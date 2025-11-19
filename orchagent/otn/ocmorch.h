#pragma once

#include "objectorch.h"

class OcmOrch: public ObjectOrch
{
public:
    OcmOrch(DBConnector *db, const std::vector<std::string> &table_names);
};


class OcmChannelOrch: public ObjectOrch
{
public:
    OcmChannelOrch(DBConnector *db, const std::vector<std::string> &table_names);
};
