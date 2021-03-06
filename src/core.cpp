#include "include/hook/Hook.h"
#include "include/core.h"
#include "include/plugin.h"
#include "include/config.h"
#include "include/update/updater.h"
#include <iostream>
#include <thread>
#include <deque>
#include <include/config.h>
#include "include/gui/PluginConfig.h"
#include <QMessageBox>
#include "include/gui/helper.h"

#define PLUGIN_NAME "TS Patch"

#ifdef WIN32
	#include "include/hook/HookWindows.h"
#else
	#include "include/hook/HookLinux.h"
#endif

using namespace std;
using namespace std::chrono;

std::string pluginId;
struct TS3Functions functions{};
hook::Hook* instance_hook = nullptr;

namespace plugin {
	struct QueuedMessage {
		std::string message;
		PluginMessageTarget target;
	};
	deque<QueuedMessage> buffered;
	bool messagesInitialized = false;

	std::string name() {
		return PLUGIN_NAME;
	}

	std::string id() {
		return pluginId;
	}

	hook::Hook* hook() {
		return instance_hook;
	}

	namespace api {
		const TS3Functions& functions() {
			return ::functions;
		}

		std::tuple<int, int, int> version_mmp() {
			std::tuple<int, int, int> response;

			auto full_version = version();
			//Trim
			while(full_version.length() > 0 && full_version.front() == ' ') full_version = full_version.substr(1);
			while(full_version.length() > 0 && full_version.back() == ' ') 	full_version = full_version.substr(0, full_version.length() - 1);

			auto mmp_index = full_version.find(' ');
			if(mmp_index == string::npos) return response;

			auto mmp_str = full_version.substr(0, mmp_index);
			auto time_str = full_version.substr(mmp_index + 1);

			//Parse mmp (major minor patch)
			{
				auto major_index = mmp_str.find('.');
				auto minor_index = mmp_str.find('.', major_index + 1);
				if(major_index == string::npos || minor_index == string::npos) return response;

				try {
					auto major = stoi(mmp_str.substr(0, major_index));
					auto minor = stoi(mmp_str.substr(major_index + 1, minor_index));
					auto patch = stoi(mmp_str.substr(minor_index + 1));
					response = make_tuple(major, minor, patch);
				} catch (std::exception& ex) {
					cerr << "failed to parse version (" << ex.what() << ")" << endl;
					return response;
				}
			}

			return response;
		};

		std::string version() {
			char* result = nullptr;
			functions().getClientLibVersion(&result);
			string res = result;
			functions().freeMemory(result);
			return res;
		}


		uint64 versionNumber() {
			uint64 number;
			functions().getClientLibVersionNumber(&number);
			return number;
		}
	}

	void message(const std::string& message, PluginMessageTarget target, bool chat) {
		auto funcs = api::functions();
		if(!messagesInitialized)
			buffered.push_back({message, target});
		else if(api::functions().printMessage)
			api::functions().printMessage(funcs.getCurrentServerConnectionHandlerID(), message.c_str(), target);
		if(chat)
			cout << message << endl;
	}

	void message(std::string message, PluginMessageTarget target) {
		message = "[" + string(PLUGIN_NAME) + "] " + message;
		plugin::message(message, target, true);
	}

	inline void guiInitialized() {
		messagesInitialized = true;
		for(const auto& e : buffered)
			message(e.message, e.target, false);
		buffered.clear();
	}
}
void ts3plugin_freeMemory(void* data) {
	if(data) free(data);
}

void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) {
	functions = funcs;
}

void ts3plugin_registerPluginID(const char* id) {
	pluginId = id;
}

const char* ts3plugin_name() {
	return PLUGIN_NAME;
}

unique_ptr<char> static_version;
const char* ts3plugin_version() {
	if(!static_version) {
		auto version = update::local_version().string();
		static_version.reset((char*) malloc(version.length()));
		memcpy(static_version.get(), version.data(), version.length());
	}
	return static_version.get();
}

int ts3plugin_apiVersion() {
	return 22;
}

const char* ts3plugin_author() {
	return "WolverinDEV";
}

const char* ts3plugin_description() {
	return "Allow servers to generate their own license";
}

int ts3plugin_init() {
	std::thread([](){
		this_thread::sleep_for(milliseconds(1000));
		plugin::guiInitialized();
	}).detach();

	auto version = plugin::api::version_mmp();
	if(get<0>(version) < 2) {
		cerr << "Invalid clientlib version!" << endl;
		plugin::message("Invalid clientlib version!", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
		plugin::message("Don't initialize TS Patch", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
		return 1;
	}
	cout << "Client Version: " << plugin::api::version() << " (Major: " << get<0>(version) << " Minor: " << get<1>(version) << " Patch: " << get<2>(version) << ")" << endl;
	cout << "Plugin Version: " << update::local_version().string() << " (Major: " << update::local_version().major << " Minor: " << update::local_version().minor << " Patch: " << update::local_version().patch << ")" << endl;

	string error;
	if(!plugin::config::parse(error)) {
		QMessageBox::critical(nullptr, "TS3 Patch", "Failed to parse TS3 patch config!\nCould not start plugin!");
		plugin::message("Don't initialize TS Patch (invalid config)", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
		plugin::message("Error: " + error, PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
	}

	thread([](){
		plugin::message("Loading hook (async)", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
		std::string hook_error;
#ifdef WIN32
		instance_hook = new hook::HookWindows64();
#else
		instance_hook = new hook::Linux64Hook();
#endif

		auto flag = instance_hook->available(hook_error);
		if(!flag) {
			plugin::message("[]---------------------------------------------[]", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
			plugin::message("      Could not inject! (No hook available)", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
			plugin::message("[]---------------------------------------------[]", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
			return;
		}

		flag = instance_hook->initializeHook(hook_error);
		if(!flag) {
			plugin::message("[]---------------------------------------------[]", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
			plugin::message("Hook " + instance_hook->name() + " could not be initialized", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
			plugin::message("Reason: " + hook_error, PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
			plugin::message("[]---------------------------------------------[]", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
			return;
		}

		flag = instance_hook->hook(hook_error);
		if(!flag) {
			plugin::message("[]---------------------------------------------[]", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
			plugin::message("Hook " + instance_hook->name() + " could not injected", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
			plugin::message("Reason: " + hook_error, PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
			plugin::message("[]---------------------------------------------[]", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
			return;
		}

		plugin::message("[]---------------------------------------------[]", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
		plugin::message("TeamSpeak 3 patch successfully injected!", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
		plugin::message("Features:", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
		plugin::message("  - Blacklist bypass", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
		plugin::message("  - [url=TeaSpeak.de]TeaSpeak.de[/url] 3.1 server join", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
		plugin::message(" ", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
		plugin::message("Plugin by WolverinDEV", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
		plugin::message("[]---------------------------------------------[]", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
	}).detach();

	update::remote_version([](update::RemoteVersion remote) {
		if(!remote.valid()) {
			plugin::message("[Updater] Update check failed (" + update::last_error() + ")! Could not check if a update is available!", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
			return;
		}
		if(remote > update::local_version()) {
			plugin::message("There is an update available!", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
			plugin::message("Update now to " + remote.string(false) + " ([url=" + remote.url + "]" + remote.url + "[/url])", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
			if(plugin::configuration->update.notify_popup) {
				runOnThread(QApplication::instance()->thread(), [remote]{
					QMessageBox::warning(nullptr, QString::fromStdString(plugin::name()), QString::fromStdString("This version is outdated!\nA newer version is available (" + remote.url + ")\nIf you're using an outdated version your TeamSpeak will may crash!"));
				});
			}
		} else if(remote == update::local_version()) {
			plugin::message("Your version is up to date :)", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
		} else if(remote < update::local_version()) {
			plugin::message("You're using a prebuild!", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
		}
	});

	return 0;
}

void ts3plugin_shutdown() {
	string error;
	if(instance_hook) {
		if(!instance_hook->unhook(error)) {
			plugin::message("Failed to unhook the TeamSpeak 3 client on unload! This could cause fatal crashes!", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
			runOnThread(QApplication::instance()->thread(), []{
				QMessageBox::critical(nullptr, QString::fromStdString(plugin::name()), QString::fromStdString("Failed to unhook the TeamSpeak 3 client on unload!\nThis could cause fatal crashes!"));
			});
		} else
			plugin::message("Successfully unloaded!", PluginMessageTarget::PLUGIN_MESSAGE_TARGET_SERVER);
	}
}


int ts3plugin_offersConfigure() {
	return PLUGIN_OFFERS_CONFIGURE_QT_THREAD;
}

void ts3plugin_configure(void* handle, void* qParentWidget) {
	gui::initialize(qParentWidget);
}