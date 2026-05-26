#include "logger.h"
#include "dbconnector.h"
#include "tokenize.h"
#include "ipprefix.h"
#include "ocsmgr.h"
#include "exec.h"
#include "shellcmd.h"
#include <swss/redisutility.h>
#include <unistd.h>
#include <chrono>
#include <fstream>
#include <regex>
#include <nlohmann/json.hpp>

using namespace std;
using namespace swss;
using json = nlohmann::json;

OcsMgr::OcsMgr(DBConnector *cfgDb, DBConnector *appDb, DBConnector *stateDb, const std::vector<std::string> &tableNames, const std::map<std::string, std::string> &tableMaps) :
        Orch(cfgDb, tableNames),
        m_appl_db(appDb),
        m_state_db(stateDb),
        m_tableMaps(tableMaps)
{
    loadDeviceExtraMap();
}

void OcsMgr::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    string cfgName = consumer.getTableName();

    /* get app table by name */
    auto itApp = m_tableMaps.find(cfgName);
    if (itApp == m_tableMaps.end())
    {
        SWSS_LOG_ERROR("ocsmgr|%s is invalid", cfgName.c_str());
        return;
    }
    const string &appName = itApp->second;
    shared_ptr<ProducerStateTable> appTable;
    auto itTable = m_appTables.find(appName);
    if (itTable == m_appTables.end())
    {
        appTable = make_shared<ProducerStateTable>(m_appl_db, appName);
        m_appTables[appName] = appTable;
    }
    else
    {
        appTable = itTable->second;
    }

    // Phase 1 bulk: collect all SET entries and DEL keys from m_toSync, then write in one batch
    std::vector<KeyOpFieldsValuesTuple> batch_set;
    std::vector<std::string> batch_del;

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        KeyOpFieldsValuesTuple t = it->second;
        string alias = kfvKey(t);
        string op = kfvOp(t);

        // query device status, skip if device is offline
        if (!isOnline(alias))
        {
            it = consumer.m_toSync.erase(it);
            continue;
        }

        SWSS_LOG_NOTICE("ocsmgr doTask, cfg=%s, app=%s, key=%s, op=%s", cfgName.c_str(), appName.c_str(), alias.c_str(), op.c_str());

        if (op == SET_COMMAND)
        {
            auto values = kfvFieldsValues(t);
            for (auto value : values)
            {
                SWSS_LOG_NOTICE("ocsmgr doTask, key=%s, value=%s", value.first.c_str(), value.second.c_str());
            }
            if (values.size())
            {
                batch_set.push_back(t);
            }
        }
        else if (op == DEL_COMMAND)
        {
            SWSS_LOG_NOTICE("Delete component: %s", alias.c_str());
            batch_del.push_back(alias);
        }

        it = consumer.m_toSync.erase(it);
    }

    // Batch write to APPL_DB once per doTask (Phase 1: ocsmgrd bulk write)
    if (!batch_del.empty())
    {
        appTable->del(batch_del);
    }
    if (!batch_set.empty())
    {
        appTable->set(batch_set);
    }
    if (!batch_set.empty() || !batch_del.empty())
    {
        auto t = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        SWSS_LOG_NOTICE("ocsmgr batch write done: %zu set, %zu del, t=%lld",
            batch_set.size(), batch_del.size(), (long long)t);
    }
}

bool OcsMgr::writeConfigToAppDb(std::shared_ptr<ProducerStateTable> &table, const std::string &alias, const std::string &field, const std::string &value)
{
    SWSS_LOG_ENTER();

    vector<FieldValueTuple> fvs;
    FieldValueTuple fv(field, value);
    fvs.push_back(fv);
    table->set(alias, fvs);

    return true;
}

bool OcsMgr::writeConfigToAppDb(std::shared_ptr<ProducerStateTable> &table, const std::string &alias, std::vector<FieldValueTuple> &field_values)
{
    SWSS_LOG_ENTER();

    table->set(alias, field_values);
    return true;
}

bool OcsMgr::isOnline(const std::string &alias)
{
    SWSS_LOG_ENTER();

    // TODO, always return true for previous test purpose
    return true;

    SWSS_LOG_DEBUG("Query device status for alias %s", alias.c_str());

    // Parse alias to get ports
    std::vector<std::string> ports = parsePorts(alias);

    for (const auto &port : ports)
    {
        if (!queryDeviceStatus(port))
        {
            SWSS_LOG_NOTICE("Alias %s is invalid due to port %s offline", alias.c_str(), port.c_str());
            return false;
        }
    }

    SWSS_LOG_NOTICE("Alias %s is valid", alias.c_str());

    return true;
}

bool OcsMgr::queryDeviceStatus(const std::string &port)
{
    SWSS_LOG_ENTER();

    SWSS_LOG_DEBUG("Query device status for port %s", port.c_str());

    std::string device_name = queryDeviceName(port);
    SWSS_LOG_DEBUG("Device name for port %s is %s", port.c_str(), device_name.c_str());

    static const std::string linecard_table("LINECARD_INFO_TABLE");
    static const std::string sep = SonicDBConfig::getSeparator(m_state_db);
    static const std::string status_online = "Online";

    std::string key = linecard_table + sep + device_name;

    if (m_state_db->exists(key))
    {
        /* query status */
        auto hash = m_state_db->hgetall(key);

        auto status = hash.find("status");
        if (status != hash.end() && status_online == status->second)
        {
            SWSS_LOG_DEBUG("Device %s is online, port %s", device_name.c_str(), port.c_str());
            return true;
        }
    }

    SWSS_LOG_DEBUG("%s is offline, port %s", device_name.c_str(), port.c_str());

    return false;
}

bool OcsMgr::loadDeviceExtraMap()
{
    SWSS_LOG_ENTER();

    std::string path("/usr/share/sonic/platform/linecard_extra_map.json");
    std::ifstream ifs(path);

    if (ifs.fail())
    {
        SWSS_LOG_ERROR("OCS init file %s not found", path.c_str());
        return false;
    }

    json j = json::parse(ifs);
    // Convert to map<string, vector<string>>
    for (auto& item : j.items()) {
        const std::string& key = item.key();
        const auto& value = item.value();
        m_deviceExtraMap[key] = value.get<std::vector<std::string>>();
    }

    ifs.close();

    SWSS_LOG_NOTICE("OCS loadDeviceExtraMap done, size=%zu", m_deviceExtraMap.size());

    return true;
}

std::string OcsMgr::queryDeviceName(const std::string &port)
{
    SWSS_LOG_ENTER();

    for (const auto& it : m_deviceExtraMap) {
        for (const auto& v : it.second) {
            if (v == port) {
                // Found
                return it.first;
            }
        }
    }

    // Not found, return alias itself
    return port;
}

std::vector<std::string> OcsMgr::parsePorts(const std::string &alias)
{
    SWSS_LOG_ENTER();

    // Match patterns like "1A", "1A-1B", "1A-2B"
    std::regex re(R"(^(\d+[AB])(?:-(\d+[AB]))?$)");
    std::smatch match;
    std::vector<std::string> ports;

    if (std::regex_match(alias, match, re)) {
        ports.push_back(match[1]);
        if (match[2].matched) {
            ports.push_back(match[2]);
        }
    }

    return ports;
}
