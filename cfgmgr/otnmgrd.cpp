#include <vector>

#include "otnmgr.h"
#include "schema.h"
#include "select.h"

using namespace std;
using namespace swss;

#define SELECT_TIMEOUT 1000

int main(int argc, char **argv)
{
    Logger::linkToDbNative("otnmgrd");
    SWSS_LOG_ENTER();

    SWSS_LOG_NOTICE("--- Starting otnmgrd ---");

    try
    {
        map<string, string> cfg_maps =
        {
            { CFG_OTN_ATTENUATOR_TABLE_NAME, APP_OTN_ATTENUATOR_TABLE_NAME },
            { CFG_OTN_OA_TABLE_NAME, APP_OTN_OA_TABLE_NAME },
            { CFG_OTN_OCM_TABLE_NAME, APP_OTN_OCM_TABLE_NAME },
            { CFG_OTN_OCM_CHANNEL_TABLE_NAME, APP_OTN_OCM_CHANNEL_TABLE_NAME },
            { CFG_OTN_OSC_TABLE_NAME, APP_OTN_OSC_TABLE_NAME },
        };

        vector<string> cfg_tables;
        for (const auto &it : cfg_maps)
        {
            cfg_tables.push_back(it.first);
        }

        DBConnector cfgDb("CONFIG_DB", 0);
        DBConnector appDb("APPL_DB", 0);

        OtnMgr otnMgr(&cfgDb, &appDb, cfg_tables, cfg_maps);

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

            auto *c = static_cast<Executor *>(sel);
            c->execute();
        }
    }
    catch (const exception &e)
    {
        SWSS_LOG_ERROR("Runtime error: %s", e.what());
    }
    return EXIT_FAILURE;
}
