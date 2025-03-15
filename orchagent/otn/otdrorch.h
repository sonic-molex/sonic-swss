#pragma once

#include "objectorch.h"

class OtdrOrch: public ObjectOrch
{
public:
    OtdrOrch(DBConnector *db, const std::vector<std::string>& table_names);
};
