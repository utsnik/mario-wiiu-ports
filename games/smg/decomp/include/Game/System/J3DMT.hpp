#pragma once

class LiveActor;
class ModelManager;

#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
extern "C" {
int smg_j3dmt_enabled();
void smg_j3dmt_begin_category(int category);
void smg_j3dmt_end_category();
int smg_j3dmt_try_queue_actor(LiveActor*);
int smg_j3dmt_try_queue_model(ModelManager*);
int smg_j3dmt_serial_calc_begin();
void smg_j3dmt_serial_calc_end();
void smg_j3dmt_calc_enter();
void smg_j3dmt_calc_exit();
}
#endif
