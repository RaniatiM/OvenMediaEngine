//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "data_structure.h"
#include "base/info/host.h"
#include <regex>

#include <base/media_route/media_route_application_observer.h>
#include <base/provider/provider.h>

//
// Orchestrator is responsible for passing commands to registered modules, such as Provider/MediaRouter/Transcoder/Publisher.
//
// Orchestrator will upgrade to perform the following roles:
//
// 1. The publisher can request the provider to create a stream.
// 2. Other modules may request Provider/Publisher traffic information. (Especially, it will be used by the RESTful API server)
// 3. Create or manage new applications.
//    For example, if some module calls Orchestrator::CreateApplication(), the Orchestrator will create a new app
//    using the APIs of Providers, MediaRouter, and Publishers as appropriate.
//
// TODO(dimiden): Modification is required so that the module can be managed per Host

class MediaRouter;

class Orchestrator
{
protected:
	struct PrivateToken
	{
	};

public:
	enum class Result
	{
		// An error occurred
		Failed,
		// Created successfully
		Succeeded,
		// The item does exists
		Exists,
		// The item does not exists
		NotExists
	};

	static Orchestrator *GetInstance()
	{
		static Orchestrator orchestrator;

		return &orchestrator;
	}

	bool ApplyOriginMap(const std::vector<info::Host> &host_list);

	/// Register the module
	///
	/// @param module Module to register
	///
	/// @return If the module is registered or passed a different type from the previously registered type, false is returned.
	/// Otherwise, true is returned.
	bool RegisterModule(const std::shared_ptr<OrchestratorModuleInterface> &module);

	/// Unregister the module
	///
	/// @param module Module to unregister
	///
	/// @return If the module is not already registered, false is returned. Otherwise, true is returned.
	bool UnregisterModule(const std::shared_ptr<OrchestratorModuleInterface> &module);

	ov::String GetVhostNameFromDomain(const ov::String &domain_name);

	/// Generate an application name for vhost/app
	///
	/// @param vhost_name A name of VirtualHost
	/// @param app_name An application name
	///
	/// @return A new application name corresponding to vhost/app
	ov::String ResolveApplicationName(const ov::String &vhost_name, const ov::String &app_name);

	///  Generate an application name for domain/app
	///
	/// @param domain_name A name of the domain
	/// @param app_name An application name
	///
	/// @return A new application name corresponding to domain/app
	ov::String ResolveApplicationNameFromDomain(const ov::String &domain_name, const ov::String &app_name);

	bool GetUrlListForLocation(const ov::String &vhost_app_name, const ov::String &stream_name, std::vector<ov::String> *url_list);

	/// Create an application and notify the modules
	///
	/// @param vhost_name A name of VirtualHost
	/// @param app_config Application configuration to create
	///
	/// @return Creation result
	///
	/// @note Automatically DeleteApplication() when application creation fails
	Result CreateApplication(const info::Host &vhost_info, const cfg::Application &app_config);
	/// Delete the application and notify the modules
	///
	/// @param app_info Application information to delete
	///
	/// @return
	///
	/// @note If an error occurs during deletion, do not recreate the application
	Result DeleteApplication(const info::Application &app_info);

	const info::Application &GetApplication(const ov::String &vhost_app_name) const;

	bool RequestPullStream(const ov::String &vhost_app_name, const ov::String &stream_name, const ov::String &url, off_t offset);
	bool RequestPullStream(const ov::String &vhost_app_name, const ov::String &stream_name, const ov::String &url)
	{
		return RequestPullStream(vhost_app_name, stream_name, url, 0);
	}

	bool RequestPullStream(const ov::String &vhost_app_name, const ov::String &stream_name, off_t offset);
	bool RequestPullStream(const ov::String &vhost_app_name, const ov::String &stream_name)
	{
		return RequestPullStream(vhost_app_name, stream_name, 0);
	}

protected:
	struct Module
	{
		Module(OrchestratorModuleType type, const std::shared_ptr<OrchestratorModuleInterface> &module)
			: type(type),
			  module(module)
		{
		}

		OrchestratorModuleType type = OrchestratorModuleType::Unknown;
		std::shared_ptr<OrchestratorModuleInterface> module = nullptr;
	};

	struct Stream
	{
		Stream(const info::Application &app_info, const std::shared_ptr<OrchestratorProviderModuleInterface> &provider, const std::shared_ptr<pvd::Stream> &provider_stream, const ov::String &full_name)
			: app_info(app_info),
			  provider(provider),
			  provider_stream(provider_stream),
			  full_name(full_name)
		{
			is_valid = true;
		}

		info::Application app_info;

		std::shared_ptr<OrchestratorProviderModuleInterface> provider;
		std::shared_ptr<pvd::Stream> provider_stream;

		ov::String full_name;

		bool is_valid = false;
	};

	struct Application : public MediaRouteApplicationObserver
	{
		Application(Orchestrator *orchestrator, const info::Application &app_info)
			: orchestrator(orchestrator),
			  app_info(app_info)
		{
		}

		//--------------------------------------------------------------------
		// Implementation of MediaRouteApplicationObserver
		//--------------------------------------------------------------------
		// Temporarily used until Orchestrator takes stream management
		bool OnCreateStream(const std::shared_ptr<info::Stream> &info) override
		{
			return orchestrator->OnCreateStream(app_info, info);
		}

		bool OnDeleteStream(const std::shared_ptr<info::Stream> &info) override
		{
			return orchestrator->OnDeleteStream(app_info, info);
		}

		bool OnSendVideoFrame(const std::shared_ptr<info::Stream> &stream, const std::shared_ptr<MediaPacket> &media_packet) override
		{
			// Ignore packets
			return true;
		}

		bool OnSendAudioFrame(const std::shared_ptr<info::Stream> &stream, const std::shared_ptr<MediaPacket> &media_packet) override
		{
			// Ignore packets
			return true;
		}

		bool OnSendFrame(const std::shared_ptr<info::Stream> &info, const std::shared_ptr<MediaPacket> &packet) override
		{
			// Ignore packets
			return true;
		}

		ObserverType GetObserverType() override
		{
			return ObserverType::Orchestrator;
		}

		Orchestrator *orchestrator = nullptr;
		info::Application app_info;
	};

	enum class ItemState
	{
		Unknown,
		// This item is applied to OriginMap
		Applied,
		// Need to check if this item has changed
		NeedToCheck,
		// This item is applied, and not changed
		NotChanged,
		// This item is not applied, and will be applied to OriginMap/OriginList
		New,
		// This item is applied, but need to change some values
		Changed,
		// This item is applied, and will be deleted from OriginMap/OriginList
		Delete
	};

	struct Origin
	{
		Origin(const cfg::OriginsOrigin &origin_config)
			: scheme(origin_config.GetPass().GetScheme()),
			  location(origin_config.GetLocation()),
			  state(ItemState::New)

		{
			for (auto &item : origin_config.GetPass().GetUrlList())
			{
				auto url = item.GetUrl();

				// Prepend "<scheme>://"
				url.Prepend("://");
				url.Prepend(scheme);

				url_list.push_back(item.GetUrl());
			}

			this->origin_config = origin_config;
		}

		bool IsValid() const
		{
			return state != ItemState::Unknown;
		}

		info::application_id_t app_id = 0U;

		ov::String scheme;

		// Origin/Location
		ov::String location;
		// Generated URL list from <Origin>.<Pass>.<URL>
		std::vector<ov::String> url_list;

		// Original configuration
		cfg::OriginsOrigin origin_config;

		// A list of streams generated by this origin rule
		std::map<info::stream_id_t, std::shared_ptr<Stream>> stream_map;

		// A flag used to determine if an item has changed
		ItemState state = ItemState::Unknown;
	};

	struct Domain
	{
		Domain(const ov::String &name)
			: name(name),
			  state(ItemState::New)
		{
			UpdateRegex();
		}

		bool IsValid() const
		{
			return state != ItemState::Unknown;
		}

		bool UpdateRegex()
		{
			// Escape special characters
			auto special_characters = std::regex(R"([[\\./+{}$^|])");
			ov::String escaped = std::regex_replace(name.CStr(), special_characters, R"(\$&)").c_str();
			// Change "*"/"?" to ".*"/".?"
			escaped = escaped.Replace("*", ".*");
			escaped = escaped.Replace("?", ".?");
			escaped.Prepend("^");
			escaped.Append("$");

			try
			{
				regex_for_domain = std::regex(escaped);
			}
			catch (std::exception &e)
			{
				return false;
			}

			return true;
		}

		// The name of Domain (eg: *.airensoft.com)
		ov::String name;
		std::regex regex_for_domain;

		// A list of streams generated by this domain rule
		std::map<info::stream_id_t, std::shared_ptr<Stream>> stream_map;

		// A flag used to determine if an item has changed
		ItemState state = ItemState::Unknown;
	};

	struct VirtualHost
	{
		VirtualHost(const info::Host &host_info)
			: host_info(host_info), state(ItemState::New)
			  
		{
		}

		void MarkAllAs(ItemState state)
		{
			this->state = state;

			for (auto &domain : domain_list)
			{
				domain.state = state;
			}

			for (auto &origin : origin_list)
			{
				origin.state = state;
			}
		}

		bool MarkAllAs(ItemState expected_old_state, ItemState state)
		{
			if (this->state != expected_old_state)
			{
				return false;
			}

			this->state = state;

			for (auto &domain : domain_list)
			{
				if (domain.state != expected_old_state)
				{
					return false;
				}

				domain.state = state;
			}

			for (auto &origin : origin_list)
			{
				if (origin.state != expected_old_state)
				{
					return false;
				}

				origin.state = state;
			}

			return true;
		}

		// Origin Host Info
		info::Host	host_info;

		// The name of VirtualHost (eg: AirenSoft-VHost)
		ov::String name;

		// Domain list
		std::vector<Domain> domain_list;

		// Origin list
		std::vector<Origin> origin_list;

		// Application list
		std::map<info::application_id_t, std::shared_ptr<Application>> app_map;

		// A flag used to determine if an item has changed
		ItemState state = ItemState::Unknown;
	};

	Orchestrator() = default;

	bool ApplyForVirtualHost(const std::shared_ptr<VirtualHost> &virtual_host);

	/// Compares a list of domains and adds them to added_domain_list if a new entry is found
	///
	/// @param domain_list The domain list
	/// @param vhost VirtualHost configuration to compare
	///
	/// @return Whether it has changed
	ItemState ProcessDomainList(std::vector<Domain> *domain_list, const cfg::Domain &domain_config) const;
	/// Compares a list of origin and adds them to added_origin_list if a new entry is found
	///
	/// @param origin_list The origin list
	/// @param new_origin_list Origin configuration to compare
	///
	/// @return Whether it has changed
	ItemState ProcessOriginList(std::vector<Origin> *origin_list, const cfg::Origins &origins_config) const;

	info::application_id_t GetNextAppId();

	std::shared_ptr<pvd::Provider> GetProviderForScheme(const ov::String &scheme);
	std::shared_ptr<OrchestratorProviderModuleInterface> GetProviderModuleForScheme(const ov::String &scheme);
	std::shared_ptr<pvd::Provider> GetProviderForUrl(const ov::String &url);

	bool ParseVHostAppName(const ov::String &vhost_app_name, ov::String *vhost_name, ov::String *real_app_name) const;

	std::shared_ptr<VirtualHost> GetVirtualHost(const ov::String &vhost_name);
	std::shared_ptr<const VirtualHost> GetVirtualHost(const ov::String &vhost_name) const;
	std::shared_ptr<VirtualHost> GetVirtualHost(const ov::String &vhost_app_name, ov::String *real_app_name);
	std::shared_ptr<const VirtualHost> GetVirtualHost(const ov::String &vhost_app_name, ov::String *real_app_name) const;

	bool GetUrlListForLocationInternal(const ov::String &vhost_app_name, const ov::String &stream_name, std::vector<ov::String> *url_list, Origin **used_origin, Domain **used_domain);

	Result CreateApplicationInternal(const ov::String &name, const info::Application &app_info);
	Result CreateApplicationInternal(const ov::String &name, info::Application *app_info);

	Result NotifyModulesForDeleteEvent(const std::vector<Module> &modules, const info::Application &app_info);
	Result DeleteApplicationInternal(const ov::String &vhost_name, info::application_id_t app_id);
	Result DeleteApplicationInternal(const info::Application &app_info);

	const info::Application &GetApplicationInternal(const ov::String &vhost_app_name) const;
	const info::Application &GetApplicationInternal(const ov::String &vhost_name, info::application_id_t app_id) const;

	bool RequestPullStreamForUrl(const ov::String &vhost_app_name, const ov::String &stream_name, const std::shared_ptr<const ov::Url> &url, off_t offset);
	bool RequestPullStreamForLocation(const ov::String &vhost_app_name, const ov::String &stream_name, off_t offset);

	// Called from Application
	bool OnCreateStream(const info::Application &app_info, const std::shared_ptr<info::Stream> &info);
	bool OnDeleteStream(const info::Application &app_info, const std::shared_ptr<info::Stream> &info);

	std::shared_ptr<MediaRouter> _media_router;

	std::atomic<info::application_id_t> _last_application_id{info::MinApplicationId};

	// Modules
	std::recursive_mutex _module_list_mutex;
	std::vector<Module> _module_list;
	std::map<OrchestratorModuleType, std::vector<std::shared_ptr<OrchestratorModuleInterface>>> _module_map;

	mutable std::recursive_mutex _virtual_host_map_mutex;
	// key: vhost_name
	std::map<ov::String, std::shared_ptr<VirtualHost>> _virtual_host_map;
	// ordered vhost list
	std::vector<std::shared_ptr<VirtualHost>> _virtual_host_list;
};