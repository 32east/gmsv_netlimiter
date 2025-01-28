#include <GarrysMod/InterfacePointers.hpp>
#include <GarrysMod/FunctionPointers.hpp>
#include <GarrysMod/FactoryLoader.hpp>
#include <GarrysMod/ModuleLoader.hpp>
#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/Lua/LuaInterface.h>
#include <eiface.h>
#include <interface.h>
#include <detouring/hook.hpp>
#include <chrono>
#include <map>
#include <inetchannel.h>

// Define CNetChan in terms of INetChannel
class CNetChan : public INetChannel
{
};

namespace global
{
    // Create a global lua state
    GarrysMod::Lua::ILuaInterface *lua = nullptr;
    
    // Create a global pointer to the original function
    FunctionPointers::CNetChan_ProcessMessages_t ProcessMessages_original = nullptr;

    // Create a pair of a uint64 and a chrono::duration
    using TimePair = std::pair<double, std::chrono::time_point<std::chrono::system_clock>>;
    Detouring::Hook ProcessMessagesHook;
    std::map<CNetChan *, TimePair> ProcessingTimes;

    bool ProcessMessages_Hook(CNetChan *Channel, bf_read &Buffer)
    {
        // Get the original function
        static FunctionPointers::CNetChan_ProcessMessages_t Trampoline = ProcessMessagesHook.GetTrampoline<FunctionPointers::CNetChan_ProcessMessages_t>();

        // Get the processing time for the client and call the original function
        auto Start = std::chrono::system_clock::now();
        bool Return = Trampoline(Channel, Buffer);
        auto End = std::chrono::system_clock::now();
        std::chrono::duration<double, std::milli> ms = End - Start;

        // Create a new entry if the client is not in the map
        if (ProcessingTimes.find(Channel) == ProcessingTimes.end())
            ProcessingTimes[Channel] = std::make_pair(0.0, End);

        // Reset the processing time if it has been more than a second since the last reset
        TimePair &Data = ProcessingTimes[Channel];
        if (std::chrono::duration_cast<std::chrono::seconds>(End - Data.second).count() >= 1)
        {
            Data.first = 0.0;
            Data.second = End;
        }

        // Add the processing time to the total
        Data.first += ms.count();

        // Check if the client has exceeded the limit
        if (Data.first >= 1000)
        {
            // Shutdown the client
            Data.first = 0.0;
            Data.second = End;
            Channel->Shutdown("Exceeded net processing time.");

            return false;
        }

        // Return the original value
        return Return;
    }

    Detouring::Hook::Target target;

    static void Initialize(GarrysMod::Lua::ILuaBase *LUA)
    {
        global::lua = reinterpret_cast<GarrysMod::Lua::ILuaInterface *>(LUA);

        // Get a pointer to the original function
        global::ProcessMessages_original = FunctionPointers::CNetChan_ProcessMessages();

        // Check if the function exists
        if (ProcessMessages_original == nullptr)
        {
            LUA->ThrowError("failed to retrieve CNetChan::ProcessMessages");
            return;
        }

        // Create a target for the hook
        global::target = Detouring::Hook::Target((void *)ProcessMessages_original);

        // Check if the target is valid
        if (!global::target.IsValid())
        {
            LUA->ThrowError("Failed to create target");
            return;
        }

        // Create the hook
        if (!global::ProcessMessagesHook.Create(global::target, reinterpret_cast<void *>(&global::ProcessMessages_Hook)))
        {
            LUA->ThrowError("Failed to create hook");
            return;
        }

        // Enable the hook
        global::ProcessMessagesHook.Enable();
    }

    static void deinitialize()
    {
        // Disable the hook
        global::ProcessMessagesHook.Destroy();
		ProcessingTimes.clear();
    }

}

GMOD_MODULE_OPEN()
{
    // Initialize the global lua state
    global::Initialize(LUA);
    return 0;
}

GMOD_MODULE_CLOSE()
{
    // Deinitialize and destroy the hook
    global::deinitialize();
    return 0;
}
