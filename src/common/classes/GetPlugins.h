/*
 *	PROGRAM:		Firebird interface.
 *	MODULE:			ImplementHelper.h
 *	DESCRIPTION:	Tools to help access plugins.
 *
 *  The contents of this file are subject to the Initial
 *  Developer's Public License Version 1.0 (the "License");
 *  you may not use this file except in compliance with the
 *  License. You may obtain a copy of the License at
 *  http://www.ibphoenix.com/main.nfs?a=ibphoenix&page=ibp_idpl.
 *
 *  Software distributed under the License is distributed AS IS,
 *  WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *  See the License for the specific language governing rights
 *  and limitations under the License.
 *
 *  The Original Code was created by Alex Peshkov
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2010 Alex Peshkov <peshkoff at mail.ru>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 *
 *
 */

#ifndef FB_COMMON_CLASSES_GET_PLUGINS
#define FB_COMMON_CLASSES_GET_PLUGINS

#include "../common/classes/ImplementHelper.h"
#include "../common/config/config.h"
#include "../common/StatusHolder.h"

namespace Firebird{
////////////////////////////////////////////////////////////////////////////////
//class GetPlugins

// Template to help with loop in the set of plugins

template <typename P>
class GetPlugins
{
private:
    typedef GetPlugins<P>                   self_type;

    GetPlugins(const self_type&);
    self_type& operator = (const self_type&);

public:
    GetPlugins(unsigned int const interfaceType,
               const char*        namesList = nullptr);

    GetPlugins(unsigned int const interfaceType,
               Config*      const knownConfig,
               const char*        namesList = nullptr);

    ~GetPlugins();

    bool hasData() const;

    const char* name() const;

    P* plugin() const;

    void next();

    void set(const char* newName);

private:
    void getPlugin();

private:
    MasterInterfacePtr masterInterface;
    PluginManagerInterfacePtr pluginInterface;
    RefPtr<IPluginSet> pluginSet;
    P* currentPlugin;
    LocalStatus ls;
    CheckStatusWrapper status;
};//class GetPlugins

////////////////////////////////////////////////////////////////////////////////
//class GetPlugins

template<typename P>
GetPlugins<P>::GetPlugins(unsigned int const interfaceType,
                          const char*        namesList)
 : masterInterface()
 , pluginInterface()
 , pluginSet(nullptr)
 , currentPlugin(nullptr)
 , ls(*getDefaultMemoryPool())
 , status(&ls)
{
    if(!namesList)
    {
        namesList=Config::getDefaultConfig()->getPlugins(interfaceType);
    }//if

    this->pluginSet.assignRefNoIncr
        (this->pluginInterface->getPlugins
          (&this->status,
           interfaceType,
           namesList,
           nullptr));

    check(&this->status);

    this->getPlugin();
}//GetPlugins

//------------------------------------------------------------------------
template<typename P>
GetPlugins<P>::GetPlugins(unsigned int const interfaceType,
                          Config*      const knownConfig,
                          const char*        namesList)
 : masterInterface()
 , pluginInterface()
 , pluginSet(nullptr)
 , currentPlugin(nullptr)
 , ls(*getDefaultMemoryPool())
 , status(&ls)
{
    //[2016-04-23] FirebirdConf requires the not null pointer to config object.
    fb_assert(knownConfig);

    const RefPtr<IFirebirdConf>
     spCfg(FB_NEW FirebirdConf(knownConfig)); //throw

    if(!namesList)
    {
        namesList=knownConfig->getPlugins(interfaceType);
    }//if

    this->pluginSet.assignRefNoIncr
        (this->pluginInterface->getPlugins
          (&this->status,
           interfaceType,
           namesList,
           spCfg));

    check(&this->status);

    this->getPlugin();
}//GetPlugins

//------------------------------------------------------------------------
template<typename P>
GetPlugins<P>::~GetPlugins()
{
    if (this->hasData())
    {
        this->pluginInterface->releasePlugin(currentPlugin);

        this->currentPlugin = nullptr;
    }
}//~GetPlugins

//------------------------------------------------------------------------
template<typename P>
bool GetPlugins<P>::hasData() const
{
    return this->currentPlugin;
}//hasData

//------------------------------------------------------------------------
template<typename P>
const char* GetPlugins<P>::name() const
{
    return this->hasData() ? this->pluginSet->getName() : nullptr;
}//name

//------------------------------------------------------------------------
template<typename P>
P* GetPlugins<P>::plugin() const
{
    return this->currentPlugin;
}//plugin

//------------------------------------------------------------------------
template<typename P>
void GetPlugins<P>::next()
{
    if (this->hasData())
    {
        this->pluginInterface->releasePlugin(currentPlugin);

        this->currentPlugin = nullptr;

        this->pluginSet->next(&this->status);

        check(&this->status);

        this->getPlugin();
    }//if
}//next

//------------------------------------------------------------------------
template<typename P>
void GetPlugins<P>::set(const char* const newName)
{
    if (this->hasData())
    {
        this->pluginInterface->releasePlugin(currentPlugin);

        this->currentPlugin = nullptr;
    }//if

    this->pluginSet->set(&this->status, newName);

    check(&this->status);

    this->getPlugin();
}//set

//------------------------------------------------------------------------
template<typename P>
void GetPlugins<P>::getPlugin()
{
    fb_assert(this->pluginSet);

    // [2016-04-24]
    //  As I understand, it is plus one to IUnknown score.
    this->currentPlugin = (P*) this->pluginSet->getPlugin(&this->status);

    check(&this->status);
}//getPlugin

////////////////////////////////////////////////////////////////////////////////
}//namespace Firebird
#endif // FB_COMMON_CLASSES_GET_PLUGINS
