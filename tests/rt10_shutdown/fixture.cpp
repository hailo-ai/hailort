// Source-subset simulation with test doubles; not an SDK object or hardware integration test.

#include "hailo/hailort.h"
#include <atomic>
#include <vector>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#define LOGGER__ERROR(...) ((void)0)
#define CHECK_SUCCESS(s,...) do {auto v=(s);if(v!=HAILO_SUCCESS)return v;}while(0)
constexpr int DeactivateCoreOpTrace=0;template<class...T>void trace_mock(T&&...){}
#define TRACE(...) trace_mock(__VA_ARGS__)
int total=0,failed=0;
void check(const std::string &n,bool ok){++total;if(!ok){++failed;std::cout<<"FAIL "<<n<<"\n";}}
struct Device {enum class Type{PCIE,INTEGRATED};Type type=Type::PCIE;hailo_status reset_result=HAILO_SUCCESS;int resets=0;Type get_type(){return type;}hailo_status reset(int){++resets;return reset_result;}};
struct ResourcesManager;
struct Control{static hailo_status reset_context_switch_state_machine(Device&);};
constexpr int HAILO_RESET_DEVICE_MODE_NN_CORE_FIXTURE=0;
class ResourcesManager {public:bool active=true;Device m_vdma_device;hailo_status fw_result=HAILO_SUCCESS;std::atomic<int>fw_calls{0};std::function<void()>fw_hook;static ResourcesManager*current;
 bool get_is_activated(){return active;}void set_is_activated(bool x){active=x;}const char*get_dev_id(){return "fixture";}hailo_status reset_state_machine();};
ResourcesManager*ResourcesManager::current=nullptr;
hailo_status Control::reset_context_switch_state_machine(Device&){auto&r=*ResourcesManager::current;++r.fw_calls;if(r.fw_hook)r.fw_hook();return r.fw_result;}
#include "manager.inc"
class VdmaConfigCoreOp {public:
 PRIVATE_STATE
 std::shared_ptr<ResourcesManager>m_resources_manager=std::make_shared<ResourcesManager>();
 hailo_status abort_result=HAILO_SUCCESS,host_result=HAILO_SUCCESS,cancel_result=HAILO_SUCCESS,unregister_result=HAILO_SUCCESS;
 std::atomic<int>aborts{0},host_stops{0},cancels{0},unregisters{0};int pending=2;std::function<void()>abort_hook,host_hook,cancel_hook;
 hailo_status abort_low_level_streams(){++aborts;if(abort_hook)abort_hook();return abort_result;}hailo_status deactivate_host_resources(){++host_stops;if(host_hook)host_hook();return host_result;}
 hailo_status cancel_pending_transfers(){++cancels;if(cancel_hook)cancel_hook();pending=cancel_result==HAILO_SUCCESS?0:1;return cancel_result;}
 hailo_status unregister_cache_update_callback(){++unregisters;return unregister_result;}int vdevice_core_op_handle(){return 42;}
 hailo_status deactivate_impl();hailo_status shutdown_impl();hailo_status shutdown();~VdmaConfigCoreOp();
};
#include "deactivate.inc"
#include "shutdown.inc"
#include "public-shutdown.inc"
#include "destructor.inc"
void setup(VdmaConfigCoreOp&c){ResourcesManager::current=c.m_resources_manager.get();}
void scenario(bool fw){VdmaConfigCoreOp c;setup(c);if(fw)c.m_resources_manager->fw_result=HAILO_DRIVER_TIMEOUT;else c.host_result=HAILO_DRIVER_OPERATION_FAILED;
 auto first=c.shutdown_impl();auto second=c.shutdown_impl();check(fw?"B-firmware-failure-latched":"A-host-failure-latched",first==(fw?HAILO_DRIVER_TIMEOUT:HAILO_DRIVER_OPERATION_FAILED)&&second==first&&c.pending==2&&c.cancels==0&&c.host_stops==1&&c.m_resources_manager->fw_calls==(fw?1:0));}
void core_tests(){
 scenario(false);scenario(true);
 const hailo_status codes[]={HAILO_SUCCESS,HAILO_INTERNAL_FAILURE,HAILO_DRIVER_TIMEOUT};
 for(auto a:codes)for(auto h:codes)for(auto f:codes)for(auto c:codes)for(auto u:codes){VdmaConfigCoreOp op;setup(op);op.abort_result=a;op.host_result=h;op.m_resources_manager->fw_result=f;op.cancel_result=c;op.unregister_result=u;
 auto deact=h!=HAILO_SUCCESS?h:f!=HAILO_SUCCESS?f:c!=HAILO_SUCCESS?c:u;auto expected=deact!=HAILO_SUCCESS?deact:a;
 auto first=op.shutdown_impl();auto second=op.shutdown_impl();auto third=op.shutdown_impl();
 bool reached_fw=h==HAILO_SUCCESS,reached_cancel=reached_fw&&f==HAILO_SUCCESS,reached_unreg=reached_cancel&&c==HAILO_SUCCESS;
 check("status-and-partial-stage-matrix",first==expected&&second==expected&&third==expected&&op.aborts==1&&op.host_stops==1&&op.m_resources_manager->fw_calls==int(reached_fw)&&op.cancels==int(reached_cancel)&&op.unregisters==int(reached_unreg)&&op.pending==(reached_cancel?(c==HAILO_SUCCESS?0:1):2));
 }
 for(bool firmware:{false,true})for(bool fail:{false,true}){VdmaConfigCoreOp op;setup(op);std::promise<void>entered,release;auto gate=release.get_future().share();auto hook=[&]{entered.set_value();gate.wait();};if(firmware){op.m_resources_manager->fw_hook=hook;op.m_resources_manager->fw_result=fail?HAILO_DRIVER_TIMEOUT:HAILO_SUCCESS;}else{op.host_hook=hook;op.host_result=fail?HAILO_DRIVER_OPERATION_FAILED:HAILO_SUCCESS;}
 auto task=std::async(std::launch::async,[&]{return op.shutdown_impl();});entered.get_future().wait();auto competing=op.shutdown_impl();release.set_value();auto first=task.get();auto later=op.shutdown_impl();
 check("concurrent-shutdown-busy-no-second-attempt",competing==HAILO_INVALID_OPERATION&&later==first&&op.aborts==1&&op.host_stops==1&&op.m_resources_manager->fw_calls==(firmware||!fail?1:0));
 }
 {VdmaConfigCoreOp op;setup(op);hailo_status reentry=HAILO_UNINITIALIZED;op.cancel_hook=[&]{reentry=op.shutdown_impl();};auto result=op.shutdown_impl();check("reentrant-callback-busy-no-wait",result==HAILO_SUCCESS&&reentry==HAILO_INVALID_OPERATION&&op.cancels==1);}
 {VdmaConfigCoreOp op;setup(op);auto a=op.shutdown_impl(),b=op.shutdown_impl();check("successful-repeat-compatible",a==HAILO_SUCCESS&&b==a&&op.host_stops==1&&op.cancels==1);}
 for(bool fail:{false,true}){VdmaConfigCoreOp op;setup(op);auto&r=*op.m_resources_manager;r.m_vdma_device.type=Device::Type::INTEGRATED;r.m_vdma_device.reset_result=fail?HAILO_DRIVER_TIMEOUT:HAILO_SUCCESS;auto first=op.shutdown_impl(),second=op.shutdown_impl();check("integrated-second-reset-stage",first==(fail?HAILO_DRIVER_TIMEOUT:HAILO_SUCCESS)&&second==first&&r.fw_calls==1&&r.m_vdma_device.resets==1&&op.cancels==(fail?0:1)&&op.pending==(fail?2:0));}
 {VdmaConfigCoreOp op;setup(op);op.m_resources_manager->active=false;op.pending=0;auto a=op.shutdown_impl(),b=op.shutdown_impl();check("initially-inactive-compatible",a==HAILO_SUCCESS&&b==a&&op.m_resources_manager->fw_calls==0);}
}

void extra_tests(){
 for(bool in_abort:{true,false})for(int peers:{1,16})for(auto terminal:{HAILO_SUCCESS,HAILO_INTERNAL_FAILURE,HAILO_TIMEOUT}){
  VdmaConfigCoreOp op;setup(op);op.host_result=terminal;
  std::promise<void>entered,release;auto gate=release.get_future().share();auto hook=[&]{entered.set_value();gate.wait();};
  if(in_abort)op.abort_hook=hook;else op.host_hook=hook;
  auto owner=std::async(std::launch::async,[&]{return op.shutdown_impl();});entered.get_future().wait();
  std::vector<std::future<hailo_status>> competitors;for(int i=0;i<peers;i++)competitors.emplace_back(std::async(std::launch::async,[&]{return op.shutdown_impl();}));
  bool rejected=true;for(auto &peer:competitors)if(peer.get()!=HAILO_INVALID_OPERATION)rejected=false;
  release.set_value();auto first=owner.get();auto repeat=op.shutdown_impl();
  check("many-callers-one-owner-both-stage-boundaries",rejected&&first==terminal&&repeat==terminal&&op.aborts==1&&op.host_stops==1);
 }
 {VdmaConfigCoreOp op;setup(op);hailo_status nested=HAILO_UNINITIALIZED;op.abort_result=HAILO_INTERNAL_FAILURE;op.abort_hook=[&]{nested=op.shutdown_impl();};auto first=op.shutdown_impl(),repeat=op.shutdown_impl();check("abort-stage-reentry-keeps-first-error",nested==HAILO_INVALID_OPERATION&&first==HAILO_INTERNAL_FAILURE&&repeat==first&&op.aborts==1&&op.host_stops==1);}
 {VdmaConfigCoreOp one,two;setup(one);one.host_result=HAILO_TIMEOUT;auto first=one.shutdown_impl();setup(two);auto second=two.shutdown_impl();check("latch-is-per-instance",first==HAILO_TIMEOUT&&second==HAILO_SUCCESS&&one.shutdown_impl()==first&&two.shutdown_impl()==second&&one.host_stops==1&&two.host_stops==1);}
 for(auto terminal:{HAILO_SUCCESS,HAILO_INTERNAL_FAILURE,HAILO_TIMEOUT}){
  VdmaConfigCoreOp op;setup(op);op.host_result=terminal;auto first=op.shutdown_impl();op.host_result=HAILO_SUCCESS;
  std::vector<std::future<bool>> readers;for(int i=0;i<16;i++)readers.emplace_back(std::async(std::launch::async,[&]{for(int j=0;j<128;j++)if(op.shutdown_impl()!=terminal)return false;return true;}));
  bool stable=true;for(auto &r:readers)stable=r.get()&&stable;
  check("terminal-result-stable-across-concurrent-queries",first==terminal&&stable&&op.aborts==1&&op.host_stops==1);
 }
}

void review_tests() {
    // A reader observes side effects through the production atomic latch, before
    // the test joins the owner. Baseline never reads the unpublished payload.
    for (auto terminal : {HAILO_INTERNAL_FAILURE, HAILO_TIMEOUT, HAILO_UNINITIALIZED}) {
        VdmaConfigCoreOp op; setup(op); op.host_result = terminal;
        int payload[3] = {0, 0, 0};
        std::promise<void> entered, release;
        auto gate = release.get_future().share();
        op.abort_hook = [&] { entered.set_value(); gate.wait(); payload[0]=17; payload[1]=29; payload[2]=43; };
        auto owner = std::async(std::launch::async, [&] { return op.shutdown(); });
        entered.get_future().wait();
        auto reader = std::async(std::launch::async, [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            hailo_status observed;
            do {
                observed = op.shutdown();
                if (observed != HAILO_INVALID_OPERATION) {
                    if (observed != terminal) return false; // Baseline: do not read payload.
                    return payload[0]==17 && payload[1]==29 && payload[2]==43;
                }
                std::this_thread::yield();
            } while (std::chrono::steady_clock::now() < deadline);
            return false;
        });
        release.set_value();
        const bool visible = reader.get(); // Deliberately before owner join.
        const auto first = owner.get();
        check("terminal-publication-before-owner-join", visible && first==terminal && op.aborts==1);
    }
    for (auto terminal : {HAILO_SUCCESS, HAILO_INTERNAL_FAILURE, HAILO_INVALID_OPERATION}) {
        int abort_calls=0, host_calls=0;
        { VdmaConfigCoreOp op; setup(op); op.host_result=terminal;
          op.abort_hook=[&]{++abort_calls;}; op.host_hook=[&]{++host_calls;};
          check("public-shutdown-terminal",op.shutdown()==terminal);
        } // Actual extracted production destructor; dependencies still alive.
        check("destructor-after-explicit-shutdown-does-not-retry",abort_calls==1 && host_calls==1);
    }
    { int abort_calls=0, host_calls=0;
      { VdmaConfigCoreOp op; setup(op); op.abort_hook=[&]{++abort_calls;}; op.host_hook=[&]{++host_calls;}; }
      check("destructor-first-invokes-shutdown-once",abort_calls==1 && host_calls==1);
    }
    { bool compatible=true;
      for(int value=0; value<static_cast<int>(HAILO_STATUS_COUNT); ++value) {
        VdmaConfigCoreOp op; setup(op); const auto terminal=static_cast<hailo_status>(value);
        op.host_result=terminal;
        const auto first=op.shutdown(), second=op.shutdown();
        compatible = compatible && first==terminal && second==terminal && op.aborts==1 && op.host_stops==1;
      }
      check("all-defined-status-values-fit-latch-and-repeat",compatible);
    }
}

int main(int argc,char**argv){std::string mode=argc>1?argv[1]:"core";if(mode=="review")review_tests();else if(mode=="extra")extra_tests();else if(mode=="minimal"){scenario(false);scenario(true);}else core_tests();std::cout<<"RESULT {\"total\":"<<total<<",\"passed\":"<<total-failed<<",\"failed\":"<<failed<<",\"skipped\":0}\n";return failed?1:0;}
