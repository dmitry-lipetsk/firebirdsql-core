/*
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
 *  The Original Code was created by Adriano dos Santos Fernandes
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2008 Adriano dos Santos Fernandes <adrianosf@uol.com.br>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#include "firebird.h"
#include "consts_pub.h"
#include "iberror.h"
#include "../yvalve/PluginManager.h"
#include "../yvalve/MasterImplementation.h"

#include "../dsql/sqlda_pub.h"
#include "../yvalve/why_proto.h"

#include "../common/os/path_utils.h"
#include "../jrd/err_proto.h"
#include "../common/isc_proto.h"
#include "../common/classes/fb_string.h"
#include "../common/classes/init.h"
#include "../common/classes/semaphore.h"
#include "../common/config/config.h"
#include "../common/config/config_file.h"
#include "../common/utils_proto.h"
#include "../common/ScanDir.h"
#include "../common/classes/GenericMap.h"
#include "../common/db_alias.h"
#include "../common/dllinst.h"

#include "../yvalve/config/os/config_root.h"


// register builtin plugins
#include "../remote/client/interface.h"

//#define DEBUG_PLUGINS

using namespace Firebird;

namespace{
/////////////////////////////////////////////////////////////////////////////////

void changeExtension(PathName& file, const char* newExt)
{
	PathName::size_type p = file.rfind(PathUtils::dir_sep);
	if (p == PathName::npos)
	{
		p = 0;
	}
	p = file.find('.', p);
	if (p == PathName::npos)
	{
		file += '.';
	}
	else
	{
		file.erase(p + 1);
	}

	file += newExt;
}//changeExtension

////////////////////////////////////////////////////////////////////////////////
//class StaticConfHolder

// Holds a reference to plugins.conf file

class StaticConfHolder
{
public:
	explicit StaticConfHolder(MemoryPool& p)
		: confFile(FB_NEW_POOL(p) ConfigFile(p,
			fb_utils::getPrefix(IConfigManager::DIR_CONF, "plugins.conf"), ConfigFile::HAS_SUB_CONF))
	{
	}

	ConfigFile* get()
	{
		return confFile;
	}

private:
	RefPtr<ConfigFile> confFile;
};//class StaticConfHolder

InitInstance<StaticConfHolder> pluginsConf;

////////////////////////////////////////////////////////////////////////////////

RefPtr<ConfigFile> findConfig(const char* param, const char* pluginName)
{
	ConfigFile* f = pluginsConf().get();
	if (f)
	{
		const ConfigFile::Parameter* plugPar = f->findParameter(param, pluginName);
		if (plugPar && plugPar->sub.hasData())
		{
			return plugPar->sub;
		}
	}

	return RefPtr<ConfigFile>(nullptr);
}//findConfig

bool flShutdown = false;

////////////////////////////////////////////////////////////////////////////////
//class ConfigParameterAccess

class ConfigParameterAccess FB_FINAL :
	public RefCntIface<IConfigEntryImpl<ConfigParameterAccess, CheckStatusWrapper> >
{
public:
	ConfigParameterAccess(IReferenceCounted* c, const ConfigFile::Parameter* p) : cf(c), par(p) { }

	// IConfigEntry implementation
	const char* getName()
	{
		return par ? par->name.c_str() : NULL;
	}

	const char* getValue()
	{
		return par ? par->value.nullStr() : NULL;
	}

	FB_BOOLEAN getBoolValue()
	{
		return par ? par->asBoolean() : 0;
	}

	ISC_INT64 getIntValue()
	{
		return par ? par->asInteger() : 0;
	}

	IConfig* getSubConfig(CheckStatusWrapper* status);

	int release()
	{
		if (--refCounter == 0)
		{
			delete this;
			return 0;
		}

		return 1;
	}

private:
	RefPtr<IReferenceCounted> cf;
	const ConfigFile::Parameter* par;
};//class ConfigParameterAccess

////////////////////////////////////////////////////////////////////////////////
//class ConfigAccess

class ConfigAccess FB_FINAL :
	public RefCntIface<IConfigImpl<ConfigAccess, CheckStatusWrapper> >
{
public:
	ConfigAccess(RefPtr<ConfigFile> c) : confFile(c) { }

	// IConfig implementation
	IConfigEntry* find(CheckStatusWrapper* status, const char* name)
	{
		try
		{
			return confFile.hasData() ? newParam(confFile->findParameter(name)) : NULL;
		}
		catch (const Firebird::Exception& ex)
		{
			ex.stuffException(status);
		}
		return NULL;
	}//find

	IConfigEntry* findValue(CheckStatusWrapper* status, const char* name, const char* value)
	{
		try
		{
			return confFile.hasData() ? newParam(confFile->findParameter(name, value)) : NULL;
		}
		catch (const Firebird::Exception& ex)
		{
			ex.stuffException(status);
		}
		return NULL;
	}//findValue

	IConfigEntry* findPos(CheckStatusWrapper* status, const char* name, unsigned int n)
	{
		try
		{
			if (!confFile.hasData())
			{
				return NULL;
			}

			const ConfigFile::Parameters& p = confFile->getParameters();
			FB_SIZE_T pos;
			if (!p.find(name, pos))
			{
				return NULL;
			}

			if (n + pos >= p.getCount() || p[n + pos].name != name)
			{
				return NULL;
			}

			return newParam(&p[n + pos]);
		}
		catch (const Firebird::Exception& ex)
		{
			ex.stuffException(status);
		}
		return NULL;
	}//findPos

	int release()
	{
		if (--refCounter == 0)
		{
			delete this;
			return 0;
		}

		return 1;
	}//release

private:
	RefPtr<ConfigFile> confFile;

	IConfigEntry* newParam(const ConfigFile::Parameter* p)
	{
		if (p)
		{
			IConfigEntry* rc = FB_NEW ConfigParameterAccess(this, p);
			rc->addRef();
			return rc;
		}

		return NULL;
	}//newParam
};//class ConfigAccess

////////////////////////////////////////////////////////////////////////////////

IConfig* ConfigParameterAccess::getSubConfig(CheckStatusWrapper* status)
{
	try
	{
		if (par && par->sub.hasData())
		{
			IConfig* rc = FB_NEW ConfigAccess(par->sub);
			rc->addRef();
			return rc;
		}
	}
	catch (const Firebird::Exception& ex)
	{
		ex.stuffException(status);
	}

	return NULL;
}//getSubConfig

////////////////////////////////////////////////////////////////////////////////

IConfig* findDefConfig(ConfigFile*     defaultConfig,
                       const PathName& confName)
{
	LocalStatus ls;

	CheckStatusWrapper s(&ls);

	if (defaultConfig)
	{
		const ConfigFile::Parameter* p = defaultConfig->findParameter("Config");
		IConfig* rc = FB_NEW ConfigAccess(p ? findConfig("Config", p->value.c_str()) : RefPtr<ConfigFile>(nullptr));
		rc->addRef();
		return rc;
	}

	IConfig* rc = PluginManagerInterfacePtr()->getConfig(&s, confName.nullStr());

	check(&s);

	return rc;
}//findDefConfig

////////////////////////////////////////////////////////////////////////////////
//struct RegisteredPlugin

// Plugins registered when loading plugin module.
// This is POD object - no dtor, only simple data types inside.

struct RegisteredPlugin
{
	RegisteredPlugin(IPluginFactory* f, const char* nm, unsigned int t)
		: factory(f), name(nm), type(t)
	{ }

	RegisteredPlugin()
		: factory(NULL), name(NULL), type(0)
	{ }

	IPluginFactory* factory;
	const char* name;
	unsigned int type;
};//struct RegisteredPlugin

////////////////////////////////////////////////////////////////////////////////
//class PluginModule

// Controls module, containing plugins.

class PluginModule : public Firebird::RefCounted, public GlobalStorage
{
public:
	PluginModule(ModuleLoader::Module* pmodule, const PathName& pname);

	unsigned int addPlugin(const RegisteredPlugin& p)
	{
		regPlugins.add(p);
		return regPlugins.getCount() - 1;
	}

	int findPlugin(unsigned int type, const PathName& name)
	{
		// typically modules do not contain too many plugins
		// therefore direct array scan is OK here
		for (unsigned int i = 0; i < regPlugins.getCount(); ++i)
		{
			if (type == regPlugins[i].type && name == regPlugins[i].name)
			{
				return i;
			}
		}

		return -1;
	}

	RegisteredPlugin& getPlugin(unsigned int i)
	{
		return regPlugins[i];
	}

	PluginModule* findModule(const PathName& pname)
	{
		if (name == pname)
		{
			return this;
		}
		if (next)
		{
			return next->findModule(pname);
		}
		return NULL;
	}

	const char* getName() const throw()
	{
		return name.nullStr();
	}

	void setCleanup(IPluginModule* c)
	{
		cleanup = c;
	}

	void resetCleanup(IPluginModule* c)
	{
		if (cleanup == c)
		{
			cleanup = 0;
#ifdef DEBUG_PLUGINS
			fprintf(stderr, "resetCleanup() of module %s\n", name.c_str());
#endif
			// This is called only by unregister module
			// when current module is forced to go away by OS.
			// Do not unload it ourselves in this case.
			addRef();
		}
		else if (next)
		{
			next->resetCleanup(c);
		}
		else
		{
			gds__log("Failed to reset cleanup %p\n", c);
		}
	}

private:
	~PluginModule()
	{
		if (next)
		{
			next->prev = prev;
		}
		*prev = next;

		if (cleanup)
		{
			// Pause timer thread for cleanup period
			MutexLockGuard timerPause(Why::pauseTimer(), FB_FUNCTION);

			cleanup->doClean();
		}
	}

	PathName name;
	Firebird::AutoPtr<ModuleLoader::Module> module;
	Firebird::IPluginModule* cleanup;
	HalfStaticArray<RegisteredPlugin, 2> regPlugins;
	PluginModule* next;
	PluginModule** prev;
};//class PluginModule

////////////////////////////////////////////////////////////////////////////////
//struct CountByType

struct CountByType
{
	int counter;
	Semaphore* waitsOn;

	CountByType()
		: counter(0), waitsOn(NULL)
	{ }
};//struct CountByType

////////////////////////////////////////////////////////////////////////////////
//struct CountByTypeArray

struct CountByTypeArray
{
	explicit CountByTypeArray(MemoryPool&)
	{}

	CountByType values[PluginManager::TYPE_COUNT];

	CountByType& get(unsigned int t)
	{
		fb_assert(t < PluginManager::TYPE_COUNT);
		return values[t];
	}
};//struct CountByTypeArray

GlobalPtr<CountByTypeArray> byTypeCounters;

PluginModule* builtin = NULL;

////////////////////////////////////////////////////////////////////////////////
//class ConfiguredPlugin

// Provides most of configuration services for plugins,
// except per-database configuration in databases.conf
class ConfiguredPlugin FB_FINAL :
	public RefCntIface<ITimerImpl<ConfiguredPlugin, CheckStatusWrapper> >
{
public:
	ConfiguredPlugin(RefPtr<PluginModule> pmodule, unsigned int preg,
					 RefPtr<ConfigFile> pconfig, const PathName& pconfName,
					 const PathName& pplugName)
		: module(pmodule)
        , regPlugin(preg)
        , defaultConfig(pconfig)
        , confName(getPool(), pconfName)
        , plugName(getPool(), pplugName)
        , delay(DEFAULT_DELAY)
	{
		if (defaultConfig.hasData())
		{
			const ConfigFile::Parameter* p = defaultConfig->findParameter("ConfigFile");
			if (p && p->value.hasData())
			{
				confName = p->value.ToPathName();
			}
		}
		if (module != builtin)
		{
			byTypeCounters->get(module->getPlugin(regPlugin).type).counter++;
		}
#ifdef DEBUG_PLUGINS
		RegisteredPlugin& r(module->getPlugin(regPlugin));
		fprintf(stderr, " ConfiguredPlugin %s module %s registered as %s type %d order %d\n",
				plugName.c_str(), module->getName(), r.name, r.type, regPlugin);
#endif
	}

	const char* getConfigFileName()
	{
		return confName.c_str();
	}

	IConfig* getDefaultConfig()
	{
		return findDefConfig(defaultConfig, confName);
	}

	const PluginModule* getPluggedModule() const throw()
	{
		return module;
	}

	IPluginBase* factory(IFirebirdConf *iFirebirdConf);

	~ConfiguredPlugin();

	const char* getPlugName()
	{
		return plugName.c_str();
	}

	void setReleaseDelay(ISC_UINT64 microSeconds) throw()
	{
#ifdef DEBUG_PLUGINS
		fprintf(stderr, "Set delay for ConfiguredPlugin %s:%p\n", plugName.c_str(), this);
#endif
		delay = microSeconds > DEFAULT_DELAY ? microSeconds : DEFAULT_DELAY;
	}

	ISC_UINT64 getReleaseDelay()
	{
		return delay;
	}

	// ITimer implementation
	void handler()
	{ }

	int release();

private:
	RefPtr<PluginModule> module;
	unsigned int regPlugin;
	RefPtr<ConfigFile> defaultConfig;
	PathName confName;
	PathName plugName;

	static const FB_UINT64 DEFAULT_DELAY = 1000000 * 60;		// 1 min
	FB_UINT64 delay;
};//class ConfiguredPlugin

////////////////////////////////////////////////////////////////////////////////
//class FactoryParameter

// Provides per-database configuration from databases.conf.

class FactoryParameter FB_FINAL :
	public RefCntIface<IPluginConfigImpl<FactoryParameter, CheckStatusWrapper> >
{
public:
	FactoryParameter(ConfiguredPlugin* cp, IFirebirdConf* fc)
		: configuredPlugin(cp)
        , firebirdConf(fc)
	{ }

	// IPluginConfig implementation
	const char* getConfigFileName()
	{
		return configuredPlugin->getConfigFileName();
	}

	IConfig* getDefaultConfig(CheckStatusWrapper* status)
	{
		try
		{
			return configuredPlugin->getDefaultConfig();
		}
		catch (const Exception& ex)
		{
			ex.stuffException(status);
			return NULL;
		}
	}//getDefaultConfig

	IFirebirdConf* getFirebirdConf(CheckStatusWrapper* status)
	{
		try
		{
			if (!firebirdConf.hasData())
			{
				RefPtr<Config> specificConf(Config::getDefaultConfig());
				firebirdConf = FB_NEW FirebirdConf(specificConf);
			}

			firebirdConf->addRef();
			return firebirdConf;
		}
		catch (const Exception& ex)
		{
			ex.stuffException(status);
			return NULL;
		}
	}//getFirebirdConf

	void setReleaseDelay(CheckStatusWrapper*, ISC_UINT64 microSeconds)
	{
		configuredPlugin->setReleaseDelay(microSeconds);
	}

	int release()
	{
		if (--refCounter == 0)
		{
			delete this;
			return 0;
		}

		return 1;
	}

private:
	~FactoryParameter()
	{
#ifdef DEBUG_PLUGINS
		fprintf(stderr, "~FactoryParameter places configuredPlugin %s in unload query for %d seconds\n",
			configuredPlugin->getPlugName(), configuredPlugin->getReleaseDelay() / 1000000);
#endif
		LocalStatus ls;
		CheckStatusWrapper s(&ls);
		TimerInterfacePtr()->start(&s, configuredPlugin, configuredPlugin->getReleaseDelay());
		// errors are ignored here - configuredPlugin will be released at once
	}

	RefPtr<ConfiguredPlugin> configuredPlugin;
	RefPtr<IFirebirdConf> firebirdConf;
};//class FactoryParameter

////////////////////////////////////////////////////////////////////////////////

IPluginBase* ConfiguredPlugin::factory(IFirebirdConf* const firebirdConf)
{
    //! \todo
    //!  Really - very unshapely design

	FactoryParameter* const par = FB_NEW FactoryParameter(this, firebirdConf);

	par->addRef();

	LocalStatus ls;

	CheckStatusWrapper s(&ls);

	IPluginBase* const plugin
     = module->getPlugin(regPlugin).factory->createPlugin(&s, par);

	if (!(s.getState() & Firebird::IStatus::STATE_ERRORS))
	{
		plugin->setOwner(par);
		return plugin;
	}

	par->release();

	check(&s);

	return NULL;
}//factory

////////////////////////////////////////////////////////////////////////////////
//class MapKey

class MapKey : public AutoStorage
{
	public:
		MapKey(unsigned int ptype, const PathName& pname)
			: type(ptype)
            , name(getPool(), pname)
		{ }

		MapKey(MemoryPool& p, const MapKey& mk)
			: AutoStorage(p)
            , type(mk.type)
            , name(getPool(), mk.name)
		{ }

		bool operator<(const MapKey& c) const	{	return type < c.type || (type == c.type && name < c.name);	}

		bool operator==(const MapKey& c) const	{	return type == c.type && name == c.name;					}

		bool operator>(const MapKey& c) const	{	return type > c.type || (type == c.type && name > c.name);	}

	private:
		unsigned int type;
		PathName name;
};//class MapKey

static bool destroyingPluginsMap = false;

////////////////////////////////////////////////////////////////////////////////
//class PluginsMap

class PluginsMap : public GenericMap<Pair<Left<MapKey, ConfiguredPlugin*> > >
{
public:
	explicit PluginsMap(MemoryPool& p)
		: GenericMap<Pair<Left<MapKey, ConfiguredPlugin*> > >(p), wakeIt(NULL)
	{
	}

	~PluginsMap()
	{
		MutexLockGuard g(mutex, FB_FUNCTION);

		destroyingPluginsMap = true;
		// unload plugins
		Accessor accessor(this);
		for (bool found = accessor.getFirst(); found; found = accessor.getNext())
		{
			ConfiguredPlugin* plugin = accessor.current()->second;
			plugin->release();
		}
	}

	Mutex mutex; // locked by this class' destructor and by objects that use the plugins var below.
	Semaphore* wakeIt;
};//class PluginsMap

GlobalPtr<PluginsMap> plugins;

////////////////////////////////////////////////////////////////////////////////

ConfiguredPlugin::~ConfiguredPlugin()
{
	if (!destroyingPluginsMap)
	{
		plugins->remove(MapKey(module->getPlugin(regPlugin).type, plugName));
	}

	fb_assert(!plugins->wakeIt);
	if (module != builtin)
	{
		unsigned int type = module->getPlugin(regPlugin).type;
		if (--(byTypeCounters->get(type).counter) == 0)
		{
			plugins->wakeIt = byTypeCounters->get(type).waitsOn;
		}
	}
#ifdef DEBUG_PLUGINS
	fprintf(stderr, "~ConfiguredPlugin %s type %d\n", plugName.c_str(), module->getPlugin(regPlugin).type);
#endif
}

////////////////////////////////////////////////////////////////////////////////

PluginModule* modules = NULL;

PluginModule* current = NULL;

////////////////////////////////////////////////////////////////////////////////

PluginModule::PluginModule(ModuleLoader::Module* pmodule, const PathName& pname)
	: name(getPool(), pname), module(pmodule), cleanup(NULL), regPlugins(getPool()),
	  next(modules), prev(&modules)
{
	if (next)
	{
		next->prev = &next;
	}
	*prev = this;
}//PluginModule

////////////////////////////////////////////////////////////////////////////////

int ConfiguredPlugin::release()
{
	MutexLockGuard g(plugins->mutex, FB_FUNCTION);

	int x = --refCounter;

#ifdef DEBUG_PLUGINS
	fprintf(stderr, "ConfiguredPlugin::release %s %d\n", plugName.c_str(), x);
#endif

	if (x == 0)
	{
		delete this;
		return 0;
	}

	return 1;
}//release

////////////////////////////////////////////////////////////////////////////////
//struct PluginLoadInfo

struct PluginLoadInfo
{
	PathName curModule, regName, plugConfigFile;
	RefPtr<ConfigFile> conf;
	bool required;

	explicit PluginLoadInfo(const char* pluginName)
	{
		// define default values for plugin ...
		curModule = fb_utils::getPrefix(IConfigManager::DIR_PLUGINS, pluginName);
		regName = pluginName;
		required = false;

		// and try to load them from conf file
		conf = findConfig("Plugin", pluginName);

		if (conf.hasData())
		{
			const ConfigFile::Parameter* v = conf->findParameter("RegisterName");
			if (v)
			{
				regName = v->value.ToPathName();
			}

			v = conf->findParameter("Module");
			if (v)
			{
				curModule = v->value.ToPathName();
			}

			v = conf->findParameter("Required");
			if (v)
			{
				required = v->asBoolean();
			}
		}

		plugConfigFile = curModule;
		changeExtension(plugConfigFile, "conf");
	}
};//struct PluginLoadInfo

////////////////////////////////////////////////////////////////////////////////
//class PluginSet

// Provides access to plugins of given type / name.

class PluginSet FB_FINAL : public RefCntIface<IPluginSetImpl<PluginSet, CheckStatusWrapper> >
{
private:
    typedef PluginSet                       self_type;

    PluginSet(const self_type&);
    self_type& operator = (const self_type&);

public:
	// IPluginSet implementation
	virtual const char* getName() const override final;

	virtual const char* getModuleName() const override final;

	virtual void set(CheckStatusWrapper* status, const char* newName) override final;

	virtual IPluginBase* getPlugin(CheckStatusWrapper* status) override final;

	virtual void next(CheckStatusWrapper* status) override final;

public:
	PluginSet(unsigned int   const pinterfaceType,
              const char*    const pnamesList,
			  IFirebirdConf* const fbConf);

	virtual int release() override final;

private:
	void loadError(const Arg::StatusVector& error)
	{
		(Arg::Gds(isc_pman_cannot_load_plugin) << currentName << error).raise();
	}

	static void CLOOP_CARG upFunction(void* /*interface*/, IStatus* status)
	{
		status->setErrors(Arg::Gds(isc_wish_list).value());
	}

private:
	unsigned int interfaceType;
	PathName namesList;

	PathName currentName;
	RefPtr<ConfiguredPlugin> currentPlugin;		// Missing data in this field indicates EOF

	RefPtr<IFirebirdConf> firebirdConf;
	MasterInterfacePtr masterInterface;

	RefPtr<PluginModule> loadModule(const PluginLoadInfo& info);
};//class PluginSet

////////////////////////////////////////////////////////////////////////////////
//class PluginSet

PluginSet::PluginSet(unsigned int   const pinterfaceType,
                     const char*    const pnamesList,
                     IFirebirdConf* const fbConf)
 : interfaceType(pinterfaceType)
 , namesList(getPool())
 , currentName(getPool())
 , currentPlugin(nullptr)
 , firebirdConf(fbConf)
{
	namesList.assign(pnamesList);

	namesList.alltrim(" \t");

	Firebird::LocalStatus s;

	Firebird::CheckStatusWrapper statusWrapper(&s);

	this->next(&statusWrapper);

	check(&statusWrapper);
}//PluginSet

//------------------------------------------------------------------------
int PluginSet::release()
{
	if (--refCounter == 0)
	{
		delete this;
		return 0;
	}

	return 1;
}//release

//------------------------------------------------------------------------
const char* PluginSet::getName() const
{
    if(this->currentPlugin.hasData())
    {
        return this->currentName.c_str();
    }

	return nullptr;
}//getName

//------------------------------------------------------------------------
const char* PluginSet::getModuleName() const
{
	if(this->currentPlugin.hasData())
    {
        return this->currentPlugin->getPluggedModule()->getName();
    }

    return nullptr;
}//getModuleName

//------------------------------------------------------------------------
void PluginSet::set(CheckStatusWrapper* status, const char* newName)
{
	try
	{
		namesList = newName;

		namesList.alltrim(" \t");

		this->next(status);
	}
	catch (const Firebird::Exception& ex)
	{
		ex.stuffException(status);
	}
}//set

//------------------------------------------------------------------------
void PluginSet::next(CheckStatusWrapper* status)
{
	try
	{
		if (currentPlugin.hasData())
		{
			currentPlugin = nullptr;
		}

		MutexLockGuard g(plugins->mutex, FB_FUNCTION);

		while (currentName.getWord(namesList, " \t,;"))
		{
			// First check - may be currentName is present among already configured plugins
			ConfiguredPlugin* tmp = NULL;

			if (plugins->get(MapKey(interfaceType, currentName), tmp))
			{
				currentPlugin = tmp;
				break;
			}

			// setup loadinfo
			PluginLoadInfo info(currentName.c_str());

			// Check if module is loaded and load it if needed
			RefPtr<PluginModule> m(modules->findModule(info.curModule));

			if (!m.hasData() && !flShutdown)
			{
				m = this->loadModule(info);
			}

			if (!m.hasData())
			{
				continue;
			}

			const int r = m->findPlugin(interfaceType, info.regName);

			if (r < 0)
			{
				loadError(Arg::Gds(isc_pman_plugin_notfound)
                           << info.curModule
                           << info.regName
                           << Arg::Num(interfaceType));
			}//if

			currentPlugin = FB_NEW ConfiguredPlugin(m, r, info.conf, info.plugConfigFile, currentName);

			plugins->put(MapKey(interfaceType, currentName), currentPlugin);

			return;
		}//while
	}
	catch (const Firebird::Exception& ex)
	{
		ex.stuffException(status);
	}
}//next

//------------------------------------------------------------------------
RefPtr<PluginModule> PluginSet::loadModule(const PluginLoadInfo& info)
{
	PathName fixedModuleName(info.curModule);

	ModuleLoader::Module* module = ModuleLoader::loadModule(fixedModuleName);

	if (!module && !ModuleLoader::isLoadableModule(fixedModuleName))
	{
		ModuleLoader::doctorModuleExtension(fixedModuleName);

		module = ModuleLoader::loadModule(fixedModuleName);
	}

	if (!module)
	{
		if (ModuleLoader::isLoadableModule(fixedModuleName))
		{
			loadError(Arg::Gds(isc_pman_module_bad) << fixedModuleName);
		}

		if (info.required)
		{
			loadError(Arg::Gds(isc_pman_module_notfound) << fixedModuleName);
		}

		return RefPtr<PluginModule>(nullptr);
	}

	RefPtr<PluginModule> rc(FB_NEW PluginModule(module, info.curModule));

	typedef void PluginEntrypoint(IMaster* masterInterface);

	PluginEntrypoint* startModule;

	if (module->findSymbol(STRINGIZE(FB_PLUGIN_ENTRY_POINT), startModule))
	{
		current = rc;

		startModule(masterInterface);

		current = NULL;

		return rc;
	}//if

	loadError(Arg::Gds(isc_pman_entrypoint_notfound) << fixedModuleName);

	return RefPtr<PluginModule>(nullptr);	// compiler warning silencer
}//loadModule

//------------------------------------------------------------------------
IPluginBase* PluginSet::getPlugin(CheckStatusWrapper* status)
{
	try
	{
		while (currentPlugin.hasData())
		{
			IPluginBase* p = currentPlugin->factory(firebirdConf);

			if (p)
				return p;

			this->next(status);

			if (status->getState() & Firebird::IStatus::STATE_ERRORS)
				break;
		}
	}
	catch (const Firebird::Exception& ex)
	{
		ex.stuffException(status);
	}

	return NULL;
}//getPlugin

/////////////////////////////////////////////////////////////////////////////////
//class BuiltinRegister

class BuiltinRegister
{
public:
	static void init()
	{
		PluginManagerInterfacePtr pi;
		Remote::registerRedirector(pi);
	}

	static void cleanup()
	{
	}
};//class BuiltinRegister

/////////////////////////////////////////////////////////////////////////////////
} // anonymous namespace

namespace Firebird {
////////////////////////////////////////////////////////////////////////////////
//class PluginManager

PluginManager::PluginManager()
{
	MutexLockGuard g(plugins->mutex, FB_FUNCTION);

	if (!builtin)
	{
		builtin = FB_NEW PluginModule(NULL, "<builtin>");
		builtin->addRef();	// Will never be unloaded
		current = builtin;
	}
}//PluginManager

//------------------------------------------------------------------------------
void PluginManager::registerPluginFactory(unsigned int    const interfaceType,
                                          const char*     const defaultName,
                                          IPluginFactory* const factory)
{
	MutexLockGuard g(plugins->mutex, FB_FUNCTION);

	if (!current)
	{
		// not good time to call this function - ignore request
		gds__log("Unexpected call to register plugin %s, type %d - ignored\n",
                 defaultName,
                 interfaceType);
		return;
	}//if

	const unsigned int r = current->addPlugin(RegisteredPlugin(factory,
                                                               defaultName,
                                                               interfaceType));
	if (current == builtin)
	{
		PathName plugConfigFile
         = fb_utils::getPrefix(IConfigManager::DIR_PLUGINS, defaultName);

		changeExtension(plugConfigFile, "conf");

		ConfiguredPlugin* const p = FB_NEW ConfiguredPlugin
                                             (RefPtr<PluginModule>(builtin),
                                              r,
									          findConfig("Plugin", defaultName),
                                              plugConfigFile,
                                              defaultName);

		p->addRef();  // Will never be unloaded

		plugins->put(MapKey(interfaceType, defaultName), p);
	}//if
}//registerPluginFactory

//------------------------------------------------------------------------------
void PluginManager::registerModule(IPluginModule* cleanup)
{
	MutexLockGuard g(plugins->mutex, FB_FUNCTION);

	if (!current)
	{
		// not good time to call this function - ignore request
		gds__log("Unexpected call to set module cleanup - ignored\n");
		return;
	}

	current->setCleanup(cleanup);
}//registerModule

//------------------------------------------------------------------------------
void PluginManager::unregisterModule(IPluginModule* cleanup)
{
	{	// guard scope
		MutexLockGuard g(plugins->mutex, FB_FUNCTION);
		modules->resetCleanup(cleanup);
	}

	// Module cleanup should be unregistered only if it's unloaded
	// and only if it's unloaded not by PluginManager, but by OS.
	// That means that task is closing unexpectedly - sooner of all
	// exit() is called by client of embedded server. Shutdown ourselves.

#ifdef WIN_NT
	if (!Firebird::dDllUnloadTID)
		Firebird::dDllUnloadTID = GetCurrentThreadId();
#endif

	fb_shutdown(5000, fb_shutrsn_exit_called);
}//unregisterModule

//------------------------------------------------------------------------------
IPluginSet* PluginManager::getPlugins(CheckStatusWrapper* const status,
                                      unsigned int        const interfaceType,
	                                  const char*         const namesList,
                                      IFirebirdConf*      const firebirdConf)
{
	try
	{
		static InitMutex<BuiltinRegister> registerBuiltinPlugins("RegisterBuiltinPlugins");

		registerBuiltinPlugins.init();

		MutexLockGuard g(plugins->mutex, FB_FUNCTION);

		IPluginSet* const rc = FB_NEW PluginSet(interfaceType,
                                                namesList,
                                                firebirdConf);
		rc->addRef();

		return rc;
	}
	catch (const Exception& ex)
	{
		ex.stuffException(status);
		return NULL;
	}
}//getPlugins

//------------------------------------------------------------------------------
void PluginManager::releasePlugin(IPluginBase* plugin)
{
	MutexLockGuard g(plugins->mutex, FB_FUNCTION);

	IReferenceCounted* parent = plugin->getOwner();

	if (plugin->release() == 0)
	{
		///fb_assert(parent);
		if (parent)
		{
			parent->release();
			if (plugins->wakeIt)
			{
				plugins->wakeIt->release();
				plugins->wakeIt = NULL;
			}
		}
	}
}//releasePlugin

//------------------------------------------------------------------------------
IConfig* PluginManager::getConfig(CheckStatusWrapper* const status,
                                  const char*         const filename)
{
	try
	{
        Firebird::MemoryPool* const pDMP=getDefaultMemoryPool();
        
        fb_assert(pDMP);
        
        RefPtr<ConfigFile> xc(FB_NEW_POOL(*pDMP) ConfigFile(*pDMP,
                                                            filename));

		IConfig* const rc = FB_NEW ConfigAccess(std::move(xc));

		rc->addRef();

		return rc;
	}
	catch (const Firebird::Exception& ex)
	{
		ex.stuffException(status);
	}

	return nullptr;
}//getConfig

//------------------------------------------------------------------------------
void PluginManager::shutdown()
{
	flShutdown = true;
}//shutdown

//------------------------------------------------------------------------------
void PluginManager::waitForType(unsigned int typeThatMustGoAway)
{
	fb_assert(typeThatMustGoAway < PluginManager::TYPE_COUNT);

	Semaphore sem;
	Semaphore* semPtr = NULL;

	{ // guard scope
		MutexLockGuard g(plugins->mutex, FB_FUNCTION);

		if (byTypeCounters->get(typeThatMustGoAway).counter > 0)
		{
			fb_assert(!byTypeCounters->get(typeThatMustGoAway).waitsOn);
			byTypeCounters->get(typeThatMustGoAway).waitsOn = semPtr = &sem;
#ifdef DEBUG_PLUGINS
			fprintf(stderr, "PluginManager::waitForType %d\n", typeThatMustGoAway);
#endif
		}
#ifdef DEBUG_PLUGINS
		else
		{
			fprintf(stderr, "PluginManager: type %d is already gone\n", typeThatMustGoAway);
		}
#endif
	}

	if (semPtr)
	{
		semPtr->enter();
	}
}//waitForType

////////////////////////////////////////////////////////////////////////////////
}// namespace Firebird

namespace {

class DirCache
{
public:
	explicit DirCache(MemoryPool &p)
		: cache(p)
	{
		cache.resize(IConfigManager::DIR_COUNT);
		for (unsigned i = 0; i < IConfigManager::DIR_COUNT; ++i)
		{
			cache[i] = fb_utils::getPrefix(i, "");
		}
	}

	const char* getDir(unsigned code)
	{
		fb_assert(code < IConfigManager::DIR_COUNT);
		return cache[code].c_str();
	}

private:
	ObjectsArray<PathName> cache;
};

InitInstance<DirCache> dirCache;

}	// anonymous namespace

namespace Firebird {

/******************************************************************************
*
*	Static instance of the root and install directories detector
*/

InitInstance<ConfigRoot> rootDetector;

////////////////////////////////////////////////////////////////////////////////
//class ConfigManager

// Generic access to all config interfaces

class ConfigManager : public AutoIface<IConfigManagerImpl<ConfigManager, CheckStatusWrapper> >
{
public:
	const char* getDirectory(unsigned code)
	{
		try
		{
			return dirCache().getDir(code);
		}
		catch (const Exception&)
		{
			return NULL;
		}
	}

	IConfig* getPluginConfig(const char* pluginName)
	{
		try
		{
			// setup loadinfo
			PluginLoadInfo info(pluginName);
			return findDefConfig(info.conf, info.plugConfigFile);
		}
		catch (const Exception&)
		{
			return NULL;
		}
	}

	IFirebirdConf* getFirebirdConf()
	{
		try
		{
			return getFirebirdConfig();
		}
		catch (const Exception&)
		{
			return NULL;
		}
	}

	IFirebirdConf* getDatabaseConf(const char* dbName)
	{
		try
		{
			PathName dummy;
			Firebird::RefPtr<Config> config;
			expandDatabaseName(dbName, dummy, &config);

			IFirebirdConf* firebirdConf = FB_NEW FirebirdConf(config);
			firebirdConf->addRef();
			return firebirdConf;
		}
		catch (const Exception&)
		{
			return NULL;
		}
	}

	const char* getInstallDirectory()
	{
		return rootDetector().getInstallDirectory();
	}

	const char* getRootDirectory()
	{
		return rootDetector().getRootDirectory();
	}
};//class ConfigManager

////////////////////////////////////////////////////////////////////////////////

static ConfigManager configManager;
IConfigManager* iConfigManager(&configManager);

////////////////////////////////////////////////////////////////////////////////
}// namespace Firebird
