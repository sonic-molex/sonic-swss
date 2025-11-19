#include "ocmorch.h"
#include "sai_serialize.h"
#include "sai_serialize_ext.h"
#include "schema.h"


extern sai_otn_ocm_api_t *sai_otn_ocm_api;

#define OTN_OCM_NOTIFICATION                         "OTN_OCM_NOTIFICATION"
#define OTN_OCM_REPLY                                "OTN_OCM_REPLY"

#define OTN_OCM_CHANNEL_NOTIFICATION                 "OTN_OCM_CHANNEL_NOTIFICATION"
#define OTN_OCM_CHANNEL_REPLY                        "OTN_OCM_CHANNEL_REPLY"
#define OTN_OCM_CHANNEL_FLEX_COUNTER_GROUP           "OTN_OCM_CHANNEL_FLEX_COUNTER"
#define OTN_OCM_CHANNEL_DEFAULT_POLLING_INTERVAL_MS  10000 // ms
#define OTN_OCM_CHANNEL_DEFAULT_ENABLED_STATE        true

OcmOrch::OcmOrch(DBConnector *db, const std::vector<std::string> &table_names) :
    ObjectOrch(db, table_names, (sai_object_type_t)SAI_OBJECT_TYPE_OTN_OCM, CounterType::OTN_OCM_ATTR)
{
    SWSS_LOG_ENTER();

    // For OCM channel pluggin
    std::string scriptPath = "otn_ocm_pluggin.lua";
    createFlexCounter(scriptPath,
                      OTN_OCM_CHANNEL_PLUGIN_FIELD,
                      OTN_OCM_CHANNEL_FLEX_COUNTER_GROUP,
                      StatsMode::READ,
                      OTN_OCM_CHANNEL_DEFAULT_POLLING_INTERVAL_MS,
                      OTN_OCM_CHANNEL_DEFAULT_ENABLED_STATE);

    m_stateTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OTN_OCM_TABLE_NAME));
    m_nameMapTable = std::unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OTN_OCM_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OTN_OCM_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OTN_OCM_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OTN_OCM_REPLY);

    m_createFunc = sai_otn_ocm_api->create_otn_ocm;
    m_removeFunc = sai_otn_ocm_api->remove_otn_ocm;
    m_setFunc = sai_otn_ocm_api->set_otn_ocm_attribute;
    m_getFunc = sai_otn_ocm_api->get_otn_ocm_attribute;

}

// OCM channel
OcmChannelOrch::OcmChannelOrch(DBConnector *db, const std::vector<std::string> &table_names) :
    ObjectOrch(db, table_names, (sai_object_type_t)SAI_OBJECT_TYPE_OTN_OCM_CHANNEL, CounterType::OTN_OCM_CHANNEL_ATTR)
{
    SWSS_LOG_ENTER();

    // For OCM channel flex counter
    createFlexCounter("",
                      "",
                      OTN_OCM_CHANNEL_FLEX_COUNTER_GROUP,
                      StatsMode::READ,
                      OTN_OCM_CHANNEL_DEFAULT_POLLING_INTERVAL_MS,
                      OTN_OCM_CHANNEL_DEFAULT_ENABLED_STATE);

    m_stateTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OTN_OCM_CHANNEL_TABLE_NAME));
    m_nameMapTable = std::unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OTN_OCM_CHANNEL_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OTN_OCM_CHANNEL_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OTN_OCM_CHANNEL_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OTN_OCM_CHANNEL_REPLY);

    m_createFunc = sai_otn_ocm_api->create_otn_ocm_channel;
    m_removeFunc = sai_otn_ocm_api->remove_otn_ocm_channel;
    m_setFunc = sai_otn_ocm_api->set_otn_ocm_channel_attribute;
    m_getFunc = sai_otn_ocm_api->get_otn_ocm_channel_attribute;

}
