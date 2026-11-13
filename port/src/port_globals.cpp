// The registry of port_globals.hpp (port/docs/netcode.md).
#include "port_globals.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <vector>

namespace
{
std::vector<const PortGlobal*>& List()
{
    static std::vector<const PortGlobal*> sList; // built during static initialisation
    return sList;
}
} // namespace

PortGlobal::PortGlobal(const void* pVar, unsigned int ogAddr, unsigned int size, const char* pName, unsigned int pointeeSize) :
    mVar(pVar), mOgAddr(ogAddr), mSize(size), mName(pName), mPointeeSize(pointeeSize)
{
    List().push_back(this);
}

void* PortGlobalAt(unsigned int ogAddr)
{
    for (size_t i = 0; i < List().size(); i++)
    {
        if (List()[i]->mOgAddr == ogAddr)
        {
            return const_cast<void*>(List()[i]->mVar);
        }
    }
    return 0;
}

void* PortGlobalAtOrDie(unsigned int ogAddr)
{
    void* p = PortGlobalAt(ogAddr);
    if (!p)
    {
        fprintf(stderr, "no global or static registered at 0x%X\n", ogAddr);
        abort();
    }
    return p;
}

const PortGlobal* const* PortGlobals(size_t* pCount)
{
    *pCount = List().size();
    return List().empty() ? 0 : &List()[0];
}
