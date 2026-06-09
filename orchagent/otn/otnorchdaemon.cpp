#include "otnorchdaemon.h"
#include "attenuatororch.h"
#include "oaorch.h"

OtnOrchDaemon::OtnOrchDaemon(DBConnector *applDb, DBConnector *configDb, DBConnector *stateDb, DBConnector *chassisAppDb, ZmqServer *zmqServer) :
    OrchDaemon(applDb, configDb, stateDb, chassisAppDb, zmqServer),
    m_applDb(applDb),
    m_configDb(configDb)
{
    SWSS_LOG_ENTER();
    SWSS_LOG_NOTICE("OtnOrchDaemon starting...");
}

bool OtnOrchDaemon::init()
{
    SWSS_LOG_ENTER();
    SWSS_LOG_NOTICE("OtnOrchDaemon init");

    /* attenuator */
    const std::vector<std::string> attenuator_tables = {
        APP_OTN_ATTENUATOR_TABLE_NAME
    };
    AttenuatorOrch *attenuatorOrch = new AttenuatorOrch(m_applDb, attenuator_tables);
    addOrchList(attenuatorOrch);

    /* OA */
    const std::vector<std::string> oa_tables = {
        APP_OTN_OA_TABLE_NAME
    };
    OaOrch *oaOrch = new OaOrch(m_applDb, oa_tables);
    addOrchList(oaOrch);

    /* Flex counter */
    std::vector<std::string> flex_counter_tables = {
        CFG_FLEX_COUNTER_TABLE_NAME
    };
    addOrchList(new FlexCounterOrch(m_configDb, flex_counter_tables));

    return true;
}
