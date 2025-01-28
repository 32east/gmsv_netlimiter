#include <GarrysMod/InterfacePointers.hpp>
#include <GarrysMod/FunctionPointers.hpp>
#include <detouring/hook.hpp>
#include <chrono>
#include <map>
#include <inetchannel.h>

class CNetChan : public INetChannel
{
public:
    virtual void Shutdown(const char* reason) = 0;
};

namespace global
{
    Detouring::Hook ProcessMessagesHook;
    std::map<CNetChan*, std::pair<double, std::chrono::time_point<std::chrono::system_clock>>> ProcessingTimes;

    bool ProcessMessages_Hook(CNetChan* Channel, bf_read& Buffer)
    {
        static auto Trampoline = ProcessMessagesHook.GetTrampoline<decltype(&ProcessMessages_Hook)>();
        if(!Channel || !Trampoline)
            return false;

        auto Start = std::chrono::system_clock::now();
        bool result = Trampoline(Channel, Buffer);
        auto End = std::chrono::system_clock::now();
        
        auto& [total_time, last_reset] = ProcessingTimes[Channel];
        if(std::chrono::duration_cast<std::chrono::seconds>(End - last_reset).count() >= 1)
        {
            total_time = 0.0;
            last_reset = End;
        }

        total_time += std::chrono::duration<double, std::milli>(End - Start).count();
        
        if(total_time >= 1000.0)
        {
            Channel->Shutdown("Exceeded net processing time");
            ProcessingTimes.erase(Channel);
            return false;
        }

        return result;
    }

    void Initialize(GarrysMod::Lua::ILuaBase* LUA)
    {
        auto ProcessMessages_ptr = FunctionPointers::CNetChan_ProcessMessages();
        if(!ProcessMessages_ptr)
        {
            LUA->ThrowError("Failed to get ProcessMessages pointer");
            return;
        }

        if(!ProcessMessagesHook.CreateDetour(
            reinterpret_cast<void*>(ProcessMessages_ptr),
            reinterpret_cast<void*>(&ProcessMessages_Hook)
        ))
        {
            LUA->ThrowError("Failed to create detour");
            return;
        }

        ProcessMessagesHook.Enable();
    }

    void Deinitialize()
    {
        ProcessMessagesHook.Disable();
        ProcessMessagesHook.Destroy();
        ProcessingTimes.clear();
    }
}

GMOD_MODULE_OPEN()
{
    global::Initialize(LUA);
    return 0;
}

GMOD_MODULE_CLOSE()
{
    global::Deinitialize();
    return 0;
}
