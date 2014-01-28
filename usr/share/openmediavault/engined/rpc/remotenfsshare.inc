<?php
/**
 * @license   http://www.gnu.org/licenses/gpl.html GPL Version 3
 * @author    Volker Theile <volker.theile@openmediavault.org>
 * @author    OpenMediaVault Plugin Developers <plugins@omv-extras.org>
 * @copyright Copyright (c) 2009-2013 Volker Theile
 * @copyright Copyright (c) 2013-2014 OpenMediaVault Plugin Developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
require_once("openmediavault/object.inc");
require_once("openmediavault/config.inc");
require_once("openmediavault/error.inc");
require_once("openmediavault/util.inc");
require_once("openmediavault/rpcservice.inc");
require_once("openmediavault/notify.inc");

class OMVRpcServiceRemotenfsshare extends OMVRpcServiceAbstract {
    /**
     * Get the RPC service name.
     */
    public function getName() {
        return "remotenfsshare";
    }

    /**
     * Initialize the RPC service.
     */
    public function initialize() {
        $this->registerMethod("getShareList");
        $this->registerMethod("getShare");
        $this->registerMethod("setShare");
        $this->registerMethod("deleteShare");
    }

    public function getShareList($params, $context) {
        global $xmlConfig;

        // Validate the RPC caller context.
        $this->validateMethodContext($context, array( "role" => OMV_ROLE_ADMINISTRATOR ));

        // Validate the parameters of the RPC service method.
        $this->validateMethodParams($params, '{
            "type"       : "object",
            "properties" : {
                "start"     : { "type" : "integer" },
                "limit"     : { '.$GLOBALS['OMV_JSONSCHEMA_COUNTFIELD'].' },
                "sortfield" : { '.$GLOBALS['OMV_JSONSCHEMA_SORTFIELD'].' },
                "sortdir"   : { '.$GLOBALS['OMV_JSONSCHEMA_SORTDIR'].' }
            }
        }');

        // Get the configuration object.
        $xpath = "//services/remotenfsshare/share";
        $objects = $xmlConfig->getList($xpath);
        if(is_null($objects))
            throw new OMVException(OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED, $xpath);

        // Add additional share informations.
        foreach($objects as $objectk => &$objectv) {
            $objectv['sharename'] = gettext("n/a");
            // Get the shared folder configuration object.
            $sfObject = $xmlConfig->get("//system/shares/sharedfolder[uuid='{$objectv['sharedfolderref']}']");
            if(!is_null($sfObject) && !empty($sfObject))
                $objectv['sharename'] = $sfObject['name'];
        }

        // Filter the result.
        return $this->applyFilter($objects, $params['start'], $params['limit'],
            $params['sortfield'], $params['sortdir']);
    }

    /**
     * Get a share config object.
     * @param params An array containing the following fields:
     *   \em uuid The UUID of the configuration object.
     * @param context The context of the caller.
     * @return The requested configuration object.
     * @throw E_CONFIG_GET_OBJECT_FAILED
     */
    public function getShare($params, $context) {
        global $xmlConfig;

        // Validate the RPC caller context.
        $this->validateMethodContext($context, array( "role" => OMV_ROLE_ADMINISTRATOR ));

        // Validate the parameters of the RPC service method.
        $this->validateMethodParams($params, '{
            "type"       : "object",
            "properties" : {
                "uuid" : { '.$GLOBALS['OMV_JSONSCHEMA_UUID'].' }
            }
        }');

        // Get the configuration object.
        $xpath = sprintf("//services/remotenfsshare/share[uuid='%s']", $params['uuid']);
        $object = $xmlConfig->get($xpath);
        if(is_null($object))
            throw new OMVException(OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED, $xpath);

        // Modify the result object.
        $object['nfs'] = boolval($object['nfs']);

        return $object;
    }

    /**
     * Set a share config object.
     * @param params The method parameters.
     * @param context The context of the caller.
     * @return The stored configuration object.
     * @throw E_CONFIG_OBJECT_UNIQUENESS
     * @throw E_CONFIG_GET_OBJECT_FAILED
     * @throw E_CONFIG_SET_OBJECT_FAILED
     */
    public function setShare($params, $context) {
        global $xmlConfig;

        // Validate the RPC caller context.
        $this->validateMethodContext($context, array( "role" => OMV_ROLE_ADMINISTRATOR ));

        // Validate the parameters of the RPC service method.
        $this->validateMethodParams($params, '{
            "type"       : "object",
            "properties" : {
                "uuid"            : { '.$GLOBALS['OMV_JSONSCHEMA_UUID_UNDEFINED'].' },
                "server"          : { "type" : "string" },
                "export"          : { "type" : "string" },
                "sharedfolderref" : { '.$GLOBALS['OMV_JSONSCHEMA_UUID'].' },
                "nfs"             : { "type" : "boolean" },
                "extraoptions"    : { "type" : "string" }
            }
        }');

        // Look for duplicates
        if(!empty($params["sharedfolderref"]) && $params['uuid'] == $GLOBALS['OMV_UUID_UNDEFINED']) {
            if(TRUE === $xmlConfig->exists("//services/remotenfsshare/share[sharedfolder='{$params["sharedfolderref"]}']")) {
                throw new OMVException(OMVErrorMsg::E_CONFIG_OBJECT_UNIQUENESS,
                    gettext("This share is already in use by another remote NFS share."));
            }
        }

        // Prepare the configuration object.
        $object = array(
            "uuid"            => ($params['uuid'] == $GLOBALS['OMV_UUID_UNDEFINED']) ? OMVUtil::uuid() : $params['uuid'],
            "server"          => $params['server'],
            "export"          => $params['export'],
            "sharedfolderref" => $params['sharedfolderref'],
            "nfs"             => array_boolval($params, 'nfs'),
            "extraoptions"    => $params['extraoptions'],
        );

        // Create fstab line
        if($object['nfs'] === true)
            $nfs = "nfs4";
        else
            $nfs = "nfs";

        if(!empty($object['extraoptions']))
            $extraOptions = $object['extraoptions'];
        else
            $extraOptions = "intr";

        $sfPath = OMVRpc::exec("ShareMgmt", "getPath", array("uuid" => $params['sharedfolderref']), $context);

        // <server>:</remote/export></local/directory><nfs-type><options> 0 0
        // server:/usr/local/pub /pub nfs rsize=8192,wsize=8192,timeo=14,intr
        $object['fstab_line'] = $object['server'].":".$object['export']." ".$sfPath." ".$nfs." ".$extraOptions." 0 0";

        // Set the configuration object.
        $success = FALSE;
        if($params['uuid'] == $GLOBALS['OMV_UUID_UNDEFINED']) {
            // Append the configuration object.
            $success = $xmlConfig->set("//services/remotenfsshare", array("share" => $object));
        } else {
            // Update the existing configuration object.
            $success = $xmlConfig->replace("//services/remotenfsshare/share[uuid='{$params['uuid']}']", $object);
        }
        if(FALSE === $success)
            throw new OMVException(OMVErrorMsg::E_CONFIG_SET_OBJECT_FAILED);

        // Notify configuration changes.
        $dispatcher = &OMVNotifyDispatcher::getInstance();
        $dispatcher->notify(($params['uuid'] == $GLOBALS['OMV_UUID_UNDEFINED']) ?
            OMV_NOTIFY_CREATE : OMV_NOTIFY_MODIFY,
            "org.openmediavault.services.remotenfsshare.share", $object);

        // Return the configuration object.
        return $object;
    }

    /**
     * Delete a share configuration object.
     * @param params An array containing the following fields:
     *   \em uuid The UUID of the configuration object.
     * @param context The context of the caller.
     * @return The deleted configuration object.
     */
    public function deleteShare($params, $context) {
        global $xmlConfig;

        // Validate the RPC caller context.
        $this->validateMethodContext($context, array( "role" => OMV_ROLE_ADMINISTRATOR ));

        // Validate the parameters of the RPC service method.
        $this->validateMethodParams($params, '{
            "type"       : "object",
            "properties" : {
                "uuid" : { '.$GLOBALS['OMV_JSONSCHEMA_UUID'].' }
            }
        }');

        // Get the configuration object.
        $xpath = sprintf("//services/remotenfsshare/share[uuid='%s']", $params['uuid']);
        $object = $xmlConfig->get($xpath);
        if(is_null($object))
            throw new OMVException(OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED, $xpath);

        $sfPath = OMVRpc::exec("ShareMgmt", "getPath", array("uuid" => $object['sharedfolderref']), $context);

        $cmd = sprintf("export LANG=C; umount %s", $sfPath);
        $this->exec($cmd);

        // Delete the configuration object.
        $object = $this->deleteConfigObjectByPath($xpath,
            "org.openmediavault.services.remotenfsshare.share");

        // Apply the changes immediately (no user interaction is wanted).
        OMVRpc::exec("Config", "applyChanges", array(
            "modules" => array("remotenfsshare"),
            "force" => TRUE
        ), $context);

        // Return the deleted configuration object.
        return $object;
    }
}

// Register the RPC service.
$rpcServiceMgr = &OMVRpcServiceMgr::getInstance();
$rpcServiceMgr->registerService(new OMVRpcServiceRemotenfsshare());
?>
