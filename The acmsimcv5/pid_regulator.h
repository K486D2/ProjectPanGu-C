#ifndef st_pid_regulatorULATOR_H
#define st_pid_regulatorULATOR_H

typedef struct {
   float32 Ref;
   float32 Fbk;
   float32 Err;
   float32 ErrPrev;
   float32 P_Term; 
   float32 I_Term; 
   float32 D_Term; 
   float32 OutNonSat;
   float32 OutLimit;
   float32 Out;
   float32 OutPrev; // for incremental pid
   float32 Kp;
   float32 Ki;
   float32 Kd;
   float32 SatDiff;
   void (*calc)();
} st_pid_regulator;
typedef st_pid_regulator *st_pid_regulator_handle;
void PID_calc(st_pid_regulator_handle);
#define st_pid_regulator_DEFAULTS { \
  /*Reference*/ 0.0, \
  /*Feedback*/ 0.0, \
  /*Control Error*/ 0.0, \
  /*Control Error from Previous Step*/ 0.0, \
  /*Proportional Term*/  0.0, \
  /*Integral Term*/  0.0, \
  /*Derivative Term*/  0.0, \
  /*Non-Saturated Output*/  0.0, \
  /*Output Limit*/  0.95, \
  /*Output*/  0.0, \
  /*Output from Previous Step*/  0.0, \
  /*Kp*/  1.0, \
  /*Ki*/  0.001, \
  /*Kd*/  0.0, \
  /*Difference between Non-Saturated Output and Saturated Output*/  0.0, \
  (void (*)(Uint32)) PID_calc \
}
extern st_pid_regulator pid1_iM;
extern st_pid_regulator pid1_iT;
extern st_pid_regulator pid1_spd;
extern st_pid_regulator pid1_pos;
extern st_pid_regulator pid1_ia;
extern st_pid_regulator pid1_ib;
extern st_pid_regulator pid1_ic;

extern st_pid_regulator pid2_iM;
extern st_pid_regulator pid2_iT;

#define pid1_id pid1_iM
#define pid1_iq pid1_iT
#define pid2_id pid2_iM
#define pid2_iq pid2_iT


void ACMSIMC_PIDTuner();

void commands(REAL *p_set_rpm_speed_command, REAL *p_set_iq_cmd, REAL *p_set_id_cmd);

#endif
