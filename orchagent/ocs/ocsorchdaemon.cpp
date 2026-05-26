#include "ocsorchdaemon.h"
#include "ocscrossconnectorch.h"
#include "ocsportorch.h"

OcsOrchDaemon::OcsOrchDaemon(DBConnector *applDb, DBConnector *configDb, DBConnector *stateDb, DBConnector *chassisAppDb, ZmqServer *zmqServer) :
    OrchDaemon(applDb, configDb, stateDb, chassisAppDb, zmqServer),
    m_applDb(applDb),
    m_configDb(configDb)
{
    SWSS_LOG_ENTER();
    SWSS_LOG_NOTICE("OcsOrchDaemon starting...");
}

bool OcsOrchDaemon::init()
{
    SWSS_LOG_ENTER();
    SWSS_LOG_NOTICE("OcsOrchDaemon init");

    /* OCS port */
    const std::vector<std::string> ocs_port_tables = {
        APP_OCS_PORT_TABLE_NAME
    };
    OcsPortOrch *ocsPortOrch = new OcsPortOrch(m_applDb, ocs_port_tables);
    addOrchList(ocsPortOrch);

    /* OCS cross connect: empty table list so orch adds consumer with no batch limit */
    const std::vector<std::string> ocs_cross_connect_tables = {};
    OcsCrossConnectOrch *ocsCrossConnectOrch = new OcsCrossConnectOrch(m_applDb, ocs_cross_connect_tables);
    addOrchList(ocsCrossConnectOrch);

    /* Flex counter */
    std::vector<std::string> flex_counter_tables = {
        CFG_FLEX_COUNTER_TABLE_NAME
    };
    addOrchList(new FlexCounterOrch(m_configDb, flex_counter_tables));

    return true;
}
