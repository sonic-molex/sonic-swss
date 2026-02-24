#include "otndeviceorch.h"
#include "schema.h"
#include "sai_serialize_otn.h"


extern sai_otn_device_api_t*            sai_otn_device_api;
extern sai_object_id_t                  gSwitchId;
extern sai_switch_api_t*                sai_switch_api;


#define OTN_DEVICE_NOTIFICATION                         "OTN_DEVICE_NOTIFICATION"
#define OTN_DEVICE_REPLY                                "OTN_DEVICE_REPLY"


OtnDeviceOrch::OtnDeviceOrch(DBConnector *db, const std::vector<std::string> &table_names)
    : ObjectOrch(db, table_names, (sai_object_type_t)SAI_OBJECT_TYPE_OTN_DEVICE)
{
    SWSS_LOG_ENTER();

    m_stateTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OTN_DEVICE_TABLE_NAME));
    m_nameMapTable = std::unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OTN_DEVICE_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OTN_DEVICE_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OTN_DEVICE_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OTN_DEVICE_REPLY);

    m_createFunc = sai_otn_device_api->create_otn_device;
    m_removeFunc = sai_otn_device_api->remove_otn_device;
    m_setFunc = sai_otn_device_api->set_otn_device_attribute;
    m_getFunc = sai_otn_device_api->get_otn_device_attribute;

    RegisterNotifications();
}

bool OtnDeviceOrch::RegisterNotifications()
{
    SWSS_LOG_ENTER();

    sai_attribute_t attr = {SAI_SWITCH_ATTR_OTN_ALARM_EVENT_NOTIFY, {0}};
    sai_status_t status = SAI_STATUS_SUCCESS;
    sai_attr_capability_t capability = {};

    status = sai_query_attribute_capability(gSwitchId, SAI_OBJECT_TYPE_SWITCH,
                                            SAI_SWITCH_ATTR_OTN_ALARM_EVENT_NOTIFY,
                                            &capability);
    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Unable to query the Otn Device event notification capability");
        return false;
    }

    if (!capability.set_implemented)
    {
        SWSS_LOG_INFO("Otn Device event notification not supported");
        return false;
    }

    /* Query the alarm event notification value before setting it */
    status = sai_switch_api->get_switch_attribute(gSwitchId, 1, &attr);
    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Unable to query the Otn Device event notification value");
        return false;
    }
    
    if (attr.value.ptr != nullptr)
    {
        SWSS_LOG_INFO("Otn Device event notification already set");
        return true;
    }

    attr.value.ptr = (void *)OnOtnDeviceAlarmNotification;

    status = sai_switch_api->set_switch_attribute(gSwitchId, &attr);
    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to register Otn Device event notification");
        return false;
    }

    SWSS_LOG_NOTICE("Otn Device event notification registered");

    return true;
}

void OtnDeviceOrch::OnOtnDeviceAlarmNotification(uint32_t count, const sai_otn_alarm_event_data_t *data)
{
    SWSS_LOG_ENTER();

    // TODO: handle the alarm notification
    SWSS_LOG_NOTICE("Otn Device alarm notification received: %s", sai_serialize_otn_alarm_event_ntf(count, data).c_str());
}
