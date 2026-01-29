#include "otnorchdaemon.h"
#include "attenuatororch.h"
#include "oaorch.h"
#include "ocmorch.h"
#include "wssorch.h"
#include "oscorch.h"

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

    /* OCM */
    const std::vector<std::string> ocm_tables = {
        APP_OTN_OCM_TABLE_NAME
    };
    OcmOrch *ocmOrch = new OcmOrch(m_applDb, ocm_tables);
    addOrchList(ocmOrch);

    /* OCM Channel */
    const std::vector<std::string> ocm_channel_tables = {
        APP_OTN_OCM_CHANNEL_TABLE_NAME
    };
    OcmChannelOrch *ocmChannelOrch = new OcmChannelOrch(m_applDb, ocm_channel_tables);
    addOrchList(ocmChannelOrch);

    /* WSS */
    const std::vector<std::string> wss_tables = {
        APP_OTN_WSS_TABLE_NAME
    };
    WssOrch *wssOrch = new WssOrch(m_applDb, wss_tables);
    addOrchList(wssOrch);

    /* WSS Spec Power */
    const std::vector<std::string> wss_spec_power_tables = {
        APP_OTN_WSS_SPEC_POWER_TABLE_NAME
    };
    WssSpecPowerOrch *wssSpecPowerOrch = new WssSpecPowerOrch(m_applDb, wss_spec_power_tables);
    addOrchList(wssSpecPowerOrch);

    /* OSC */
    const std::vector<std::string> osc_tables = {
        APP_OTN_OSC_TABLE_NAME
    };
    OscOrch *oscOrch = new OscOrch(m_applDb, osc_tables);
    addOrchList(oscOrch);

    /* Flex counter */
    std::vector<std::string> flex_counter_tables = {
        CFG_FLEX_COUNTER_TABLE_NAME
    };
    addOrchList(new FlexCounterOrch(m_configDb, flex_counter_tables));

    return true;
}
