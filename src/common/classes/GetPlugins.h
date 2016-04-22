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

namespace Firebird {

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
               const char*        namesList = NULL)
		: masterInterface()
        , pluginInterface()
        , pluginSet(NULL)
        , currentPlugin(NULL)
        , ls(*getDefaultMemoryPool())
        , status(&ls)
	{
        if(!namesList)
        {
            namesList=Config::getDefaultConfig()->getPlugins(interfaceType);
        }//if

		this->pluginSet.assignRefNoIncr
         (this->pluginInterface->getPlugins
           (&status,
            interfaceType,
			namesList,
			NULL));

		check(&status);

		this->getPlugin();
	}//GetPlugins

	GetPlugins(unsigned int const interfaceType,
			   Config*      const knownConfig,
               const char*        namesList = NULL)
		: masterInterface()
        , pluginInterface()
        , pluginSet(NULL)
        , currentPlugin(NULL)
        , ls(*getDefaultMemoryPool())
        , status(&ls)
	{
        const RefPtr<IFirebirdConf>
         spCfg(FB_NEW FirebirdConf(knownConfig)); //throw

        if(!namesList)
        {
            fb_assert(knownConfig);

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

	~GetPlugins()
	{
		if (this->hasData())
		{
			this->pluginInterface->releasePlugin(currentPlugin);

			this->currentPlugin = NULL;
		}
	}//~GetPlugins

	bool hasData() const
	{
		return this->currentPlugin;
	}

	const char* name() const
	{
		return this->hasData() ? this->pluginSet->getName() : NULL;
	}

	P* plugin() const
	{
		return this->currentPlugin;
	}

	void next()
	{
		if (this->hasData())
		{
			this->pluginInterface->releasePlugin(currentPlugin);
			
            this->currentPlugin = NULL;

			this->pluginSet->next(&this->status);

			check(&this->status);

			this->getPlugin();
		}
	}//next

	void set(const char* newName)
	{
		if (this->hasData())
		{
			this->pluginInterface->releasePlugin(currentPlugin);

			this->currentPlugin = NULL;
		}

		this->pluginSet->set(&this->status, newName);

		check(&this->status);

		this->getPlugin();
	}//set

private:
	void getPlugin()
	{
        fb_assert(this->pluginSet);

		this->currentPlugin = (P*) this->pluginSet->getPlugin(&this->status);

		check(&this->status);
	}//getPlugin

private:
	MasterInterfacePtr masterInterface;
	PluginManagerInterfacePtr pluginInterface;
	RefPtr<IPluginSet> pluginSet;
	P* currentPlugin;
	LocalStatus ls;
	CheckStatusWrapper status;
};//class GetPlugins

} // namespace Firebird


#endif // FB_COMMON_CLASSES_GET_PLUGINS
