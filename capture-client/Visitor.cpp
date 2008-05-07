#include "Visitor.h"
#include "Url.h"
#include "Thread.h"
#include "ApplicationPlugin.h"
#include "EventController.h"
#include "VisitEvent.h"
#include <exception>
#include <iostream>
#include <fstream>
#include <boost/bind.hpp>
#include <boost\regex.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/find_iterator.hpp>
#include <boost/algorithm/string/finder.hpp> 
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>
#include <boost/bind.hpp>

//<string::iterator>

Visitor::Visitor(void)
{
	printf("here");
	DebugPrintTrace(L"Visitor::Visitor(void) start\n");
	
	visiting = false;

	hQueueNotEmpty = CreateEvent(NULL, FALSE, FALSE, NULL);

	onServerEventConnection = EventController::getInstance()->connect_onServerEvent(L"visit-event", boost::bind(&Visitor::onServerEvent, this, _1));
	//EventController::getInstance()->attach(L"url-group", this);

	loadClientPlugins();

	visitorThread = new Thread(this);
	visitorThread->start("Visitor");
	DebugPrintTrace(L"Visitor::Visitor(void) end\n");
}

Visitor::~Visitor(void)
{
	DebugPrintTrace(L"Visitor::~Visitor(void) start\n");
	CloseHandle(hQueueNotEmpty);
	unloadClientPlugins();

	DebugPrintTrace(L"Visitor::~Visitor(void) end\n");
	// TODO free items in toVisit queue
}

void
Visitor::run()
{
	try {
		DebugPrintTrace(L"Visitor::run() start\n");
		while(true)
		{
			WaitForSingleObject(hQueueNotEmpty, INFINITE);
			VisitEvent* visitEvent = toVisit.front();
			toVisit.pop();
			visiting = true;

			DebugPrintTrace(L"Visitor::run() getting App plugin\n");
			ApplicationPlugin* applicationPlugin = getApplicationPlugin(visitEvent->getProgram());
			

			_ASSERT(applicationPlugin != NULL);
			DebugPrintTrace(L"Visitor::run() got App plugin\n");
			if(applicationPlugin)
			{
				DebugPrint(L"Visitor::run() setting algorithm\n");
				visitEvent->setAlgorithm(applicationPlugin->getAlgorithm());

				DebugPrintTrace(L"Visitor::run() notify prestart\n");
				notify(CAPTURE_VISITATION_PRESTART, *visitEvent);

				DebugPrintTrace(L"Visitor::run() notify start\n");
				notify(CAPTURE_VISITATION_START, *visitEvent);

				// Send the group of urls to the appropiate application plugin

				try
				{
					DebugPrintTrace(L"Visitor::run() visit group start\n");
					applicationPlugin->visitGroup(visitEvent);
					DebugPrintTrace(L"Visitor::run() visit group end\n");
				} 
				catch (...)
				{
					Warn(L"Visitor::run() method caught exception from visitGroup call.\n");
					notify(CAPTURE_VISITATION_EXCEPTION, *visitEvent);
				}

				// If there are errors report it else finish visitation
				if(visitEvent->isError())
				{
					DebugPrintTrace(L"Visitor::run() notify visitEvent isError\n");
					notify(visitEvent->getErrorCode(), *visitEvent);
				} else {
					DebugPrintTrace(L"Visitor::run() notify visitiation finish\n");
					notify(CAPTURE_VISITATION_FINISH, *visitEvent);
				}

				DebugPrintTrace(L"Visitor::run() notify postfinish\n");
				notify(CAPTURE_VISITATION_POSTFINISH, *visitEvent);
			}

			delete visitEvent;
			visiting = false;
			DebugPrintTrace(L"Visitor::run() end\n");
		}
	} catch (...) {
		printf("Visitor::run exception\n");	
		throw;
	}
}

void
Visitor::loadClientPlugins()
{
	DebugPrintTrace(L"Visitor::loadClientPlugins() start\n");

	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	wchar_t pluginDirectoryPath[1024];

	GetFullPathName(L"plugins\\Application_*.dll", 1024, pluginDirectoryPath, NULL);
	DebugPrint(L"Capture-Visitor: Plugin directory - %ls\n", pluginDirectoryPath);
	hFind = FindFirstFile(pluginDirectoryPath, &FindFileData);

	if (hFind != INVALID_HANDLE_VALUE) 
	{
		typedef void (*AppPlugin)(void*);
		do
		{
			std::wstring pluginDir = L"plugins\\";
			pluginDir += FindFileData.cFileName;			
			HMODULE hPlugin = LoadLibrary(pluginDir.c_str());

			if(hPlugin != NULL)
			{
				list<ApplicationPlugin*>* apps = new std::list<ApplicationPlugin*>();
				applicationPlugins.insert(PluginPair(hPlugin, apps));
				ApplicationPlugin* applicationPlugin = createApplicationPluginObject(hPlugin);
				if(applicationPlugin == NULL) {
					FreeLibrary(hPlugin);
				} else {
					printf("Loaded plugin: %ls\n", FindFileData.cFileName);
					unsigned int g = applicationPlugin->getPriority();
					wchar_t** supportedApplications = applicationPlugin->getSupportedApplicationNames();
					for(int i = 0; supportedApplications[i] != NULL; i++)
					{
						stdext::hash_map<std::wstring, ApplicationPlugin*>::iterator it;
						it = applicationMap.find(supportedApplications[i]);
						/* Check he application isn't already being handled by a plugin */
						if(it != applicationMap.end())
						{
							/* Check the priority of the existing application plugin */
							unsigned int p = it->second->getPriority();
							if(applicationPlugin->getPriority() > p)
							{
								/* Over ride the exisiting plugin if the priority of the loaded one
								   is greater */
								applicationMap.erase(supportedApplications[i]);
								printf("\toverride: added application: %ls\n", supportedApplications[i]);
								applicationMap.insert(ApplicationPair(supportedApplications[i], applicationPlugin));
							} else {
								printf("\tplugin overridden: not adding application: %ls\n", supportedApplications[i]);
							}
						} else {
							printf("\tinserted: added application: %ls\n", supportedApplications[i]);
							applicationMap.insert(ApplicationPair(supportedApplications[i], applicationPlugin)); 
						}
					}
				}
			} else {
				printf("Unable to load library.\n");
			}
		} while(FindNextFile(hFind, &FindFileData) != 0);
		FindClose(hFind);
	} else {
		printf("Unable to open first plugin.\n");
	}
	DebugPrintTrace(L"Visitor::loadClientPlugins() end\n");
}

ApplicationPlugin*
Visitor::getApplicationPlugin(const std::wstring& applicationName)
{
	DebugPrintTrace(L"Visitor::getApplicationPlugin(const std::wstring& applicationName) start\n");
	stdext::hash_map<std::wstring, ApplicationPlugin*>::iterator it;
	it = applicationMap.find(applicationName);
	if(it != applicationMap.end())
	{
		DebugPrintTrace(L"Visitor::getApplicationPlugin(const std::wstring& applicationName) end\n");
		return it->second;
	}
	DebugPrintTrace(L"Visitor::getApplicationPlugin(const std::wstring& applicationName) end\n");
	return NULL;
}

ApplicationPlugin*
Visitor::createApplicationPluginObject(HMODULE hPlugin)
{
	DebugPrintTrace(L"Visitor::createApplicationPluginObject(HMODULE hPlugin) start\n");
	typedef void (*PluginExportInterface)(void*);
	PluginExportInterface pluginCreateInstance = NULL;
	ApplicationPlugin* applicationPlugin = NULL;
	/* Get the function address to create a plugin object */
	pluginCreateInstance = (PluginExportInterface)GetProcAddress(hPlugin,"New");
	/* Create a new plugin object in the context of the plugin */
	pluginCreateInstance(&applicationPlugin);
	/* If the object was created then add it to a list so we can track it */
	if(applicationPlugin != NULL)
	{
		stdext::hash_map<HMODULE, std::list<ApplicationPlugin*>*>::iterator it;
		it = applicationPlugins.find(hPlugin);
		if(it != applicationPlugins.end())
		{
			list<ApplicationPlugin*>* apps = it->second;
			apps->push_back(applicationPlugin);
		}
	}
	DebugPrintTrace(L"Visitor::createApplicationPluginObject(HMODULE hPlugin) end\n");
	return applicationPlugin;
}

void
Visitor::unloadClientPlugins()
{
	DebugPrintTrace(L"Visitor::unloadClientPlugins() start\n");
	typedef void (*PluginExportInterface)(void*);
	stdext::hash_map<HMODULE, std::list<ApplicationPlugin*>*>::iterator it;
	for(it = applicationPlugins.begin(); it != applicationPlugins.end(); it++)
	{
		std::list<ApplicationPlugin*>::iterator lit;
		list<ApplicationPlugin*>* apps = it->second;
		PluginExportInterface pluginDeleteInstance = (PluginExportInterface)GetProcAddress(it->first,"Delete");
		for(lit = apps->begin(); lit != apps->end(); lit++)
		{
			pluginDeleteInstance(&(*lit));
		}
		delete apps;
		FreeLibrary(it->first);
	}
	DebugPrintTrace(L"Visitor::unloadClientPlugins() end\n");
}

Url*
Visitor::createUrl(const std::vector<Attribute>& attributes)
{
	DebugPrintTrace(L"Visitor::createUrl(const std::vector<Attribute>& attributes) start\n");
	std::wstring url;
	std::wstring program;
	int time = 0;
	for each(Attribute attribute in attributes)
	{
		if(attribute.getName() == L"url") {
			url = attribute.getValue();
		} else if(attribute.getName() == L"program") {
			program = attribute.getValue();
		} else if(attribute.getName() == L"time") {
			time = boost::lexical_cast<int>(attribute.getValue());
		}
	}
	DebugPrintTrace(L"Visitor::createUrl(const std::vector<Attribute>& attributes) end\n");
	return new Url(Url::decode(url), program, time);
}

void
Visitor::onServerEvent(const Element& element)
{
	DebugPrintTrace(L"Visitor::onServerEvent(const Element& element) start\n");
	VisitEvent* visitEvent = new VisitEvent();

	if(element.getName() == L"visit-event") {
		// A url event with multiple urls to visit
		std::wstring identifier = element.getAttributeValue(L"identifier");
		std::wstring program = element.getAttributeValue(L"program");
		int visitTime = boost::lexical_cast<int>(element.getAttributeValue(L"time"));
		
		visitEvent->setIdentifier(identifier);
		visitEvent->setProgram(program);

		for each(Element* e in element.getChildElements())
		{
			if(e->getName() == L"item")
			{
				Url* url = createUrl(e->getAttributes());
				// Force the visit time and program to the url event just in case
				// some supplied them in the item element
				url->setVisitTime(visitTime);
				url->setProgram(program);
				visitEvent->addUrl(url);
			}
		}	
	}

	if(visitEvent->getUrls().size() > 0)
	{
		toVisit.push(visitEvent);
		SetEvent(hQueueNotEmpty);
	} else {
		printf("Visitor-onServerEvent: ERROR no url specified for visit event\n");
		delete visitEvent;
	}
	DebugPrintTrace(L"Visitor::onServerEvent(const Element& element) end\n");
}
