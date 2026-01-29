#include <fstream>
#include <iostream>
#include <mutex>
#include <unistd.h>
#include <vector>

#include "exec.h"
#include "otnmgr.h"
#include "schema.h"
#include "select.h"

using namespace std;
using namespace swss;

/* select() function timeout retry time, in millisecond */
#define SELECT_TIMEOUT 1000

int main(int argc, char **argv)
{
    Logger::linkToDbNative("OtnMgrd");
    SWSS_LOG_ENTER();

    SWSS_LOG_NOTICE("--- Starting OtnMgrd ---");

    try
    {
        map<string, string> cfg_maps =
        {
            { CFG_OTN_ATTENUATOR_TABLE_NAME, APP_OTN_ATTENUATOR_TABLE_NAME },
            { CFG_OTN_OA_TABLE_NAME, APP_OTN_OA_TABLE_NAME },
            { CFG_OTN_OCM_TABLE_NAME, APP_OTN_OCM_TABLE_NAME },
            { CFG_OTN_OCM_CHANNEL_TABLE_NAME, APP_OTN_OCM_CHANNEL_TABLE_NAME },
            { CFG_OTN_WSS_TABLE_NAME, APP_OTN_WSS_TABLE_NAME },
            { CFG_OTN_WSS_SPEC_POWER_TABLE_NAME, APP_OTN_WSS_SPEC_POWER_TABLE_NAME },
            { CFG_OTN_OSC_TABLE_NAME, APP_OTN_OSC_TABLE_NAME }
        };

        vector<string> cfg_tables;
        for (auto const &it : cfg_maps)
        {
            cfg_tables.push_back(it.first);
        }

        DBConnector cfgDb("CONFIG_DB", 0);
        DBConnector appDb("APPL_DB", 0);
        DBConnector stateDb("STATE_DB", 0);

        OtnMgr otnMgr(&cfgDb, &appDb, &stateDb, cfg_tables, cfg_maps);

        // TODO: add tables in stateDB which interface depends on to monitor list
        vector<Orch *> cfgOrchList = { &otnMgr };

        swss::Select s;
        for (Orch *o : cfgOrchList)
        {
            s.addSelectables(o->getSelectables());
        }

        while (true)
        {
            Selectable *sel;
            int ret;

            ret = s.select(&sel, SELECT_TIMEOUT);
            if (ret == Select::ERROR)
            {
                SWSS_LOG_NOTICE("Error: %s!", strerror(errno));
                continue;
            }
            if (ret == Select::TIMEOUT)
            {
                otnMgr.doTask();
                continue;
            }

            auto *c = (Executor *)sel;
            c->execute();
        }
    }
    catch (const exception &e)
    {
        SWSS_LOG_ERROR("Runtime error: %s", e.what());
    }
    return -1;
}
