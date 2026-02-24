#pragma once

#include "objectorch.h"


class OtnDeviceOrch : public ObjectOrch
{
public:
    OtnDeviceOrch(DBConnector *db, const std::vector<std::string> &table_names);

protected:
    bool RegisterNotifications();

private:
    static void OnOtnDeviceAlarmNotification(uint32_t count, const sai_otn_alarm_event_data_t *data);
};
