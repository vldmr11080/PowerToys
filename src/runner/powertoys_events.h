#pragma once
#include <interface/powertoy_module_interface.h>
#include <string>

class PowertoysEvents {
public:
  void register_receiver(const std::wstring& event, PowertoyModuleIface* module);
  void unregister_receiver(PowertoyModuleIface* module);
  intptr_t signal_event(const std::wstring& event, intptr_t data);
private:
  std::shared_mutex mutex;
  std::unordered_map<std::wstring, std::vector<PowertoyModuleIface*>> receivers;
};

PowertoysEvents& powertoys_events();

void first_subscribed(const std::wstring& event);
void last_unsubscribed(const std::wstring& event);

