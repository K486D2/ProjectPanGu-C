#include "ACMSim.h"

// sv_count bug
// st_InverterNonlinearity t_inv = {0};

#if MACHINE_TYPE == 2
void experiment_init(){
    CTRL_init();    // 控制器结构体初始化
    rk4_init();     // 龙格库塔法结构体初始化
    pmsm_observers_init(); // 永磁电机观测器初始化
        // harnefors_init(); // harnefors结构体初始化
        // cjheemf_init(); // cjheemf 结构体初始化
    // COMM_init(); // 参数自整定初始化
}


// 初始化函数
void CTRL_init(){

    allocate_CTRL(&CTRL);

    /* Basic quantities */
    CTRL.timebase = 0.0;

    /* Peripheral configurations */
    #if PC_SIMULATION
        CTRL.enc->OffsetCountBetweenIndexAndUPhaseAxis = 0;
    #else
        CTRL.enc->OffsetCountBetweenIndexAndUPhaseAxis = SYSTEM_QEP_CALIBRATED_ANGLE;
    #endif
    CTRL.enc->theta_d_offset = SYSTEM_QEP_CALIBRATED_ANGLE * CNT_2_ELEC_RAD;

    // 这个在仿真中是要一直运行的，不要清零！
    // ENC.sum_qepPosCnt = 0.0;
    // ENC.cursor = 0;

    /* Controller quantities */
    // commands
    // CTRL.I->idq_cmd[0] = 0.0;
    // CTRL.I->idq_cmd[1] = 0.0;
    // // error
    // CTRL.omg_ctrl_err = 0.0;
    // CTRL.speed_ctrl_err = 0.0;
    // // feedback
    // CTRL.I->omg_elec = 0.0;
    // CTRL.I->theta_d_elec = 0.0;
    // CTRL.I->iab[0] = 0.0;
    // CTRL.I->iab[1] = 0.0;
    // CTRL.I->idq[0] = 0.0;
    // CTRL.I->idq[1] = 0.0;
    // CTRL.psi_mu_al__fb = 0.0;
    // CTRL.psi_mu_be__fb = 0.0;
    // CTRL.Tem = 0.0;
    // // indirect field oriented control
    CTRL.S->cosT = 1.0;
    CTRL.S->sinT = 0.0;
    // CTRL.S->omega_syn = 0.0;
    CTRL.S->vc_count = 1; // starts from 1

    CTRL.S->ctrl_strategy = CONTROL_STRATEGY;
    CTRL.S->go_sensorless = SENSORLESS_CONTROL;

    inverterNonlinearity_Initialization();

    /* Machine parameters */
    // elec
    CTRL.motor->R  = PMSM_RESISTANCE;
    CTRL.motor->KE = PMSM_PERMANENT_MAGNET_FLUX_LINKAGE; // * (0.1/0.1342); // 【实验编号：】
    CTRL.motor->Ld = PMSM_D_AXIS_INDUCTANCE;
    CTRL.motor->Lq = PMSM_Q_AXIS_INDUCTANCE;
    CTRL.motor->Lq_inv = 1.0/CTRL.motor->Lq;
    CTRL.motor->DeltaL = CTRL.motor->Ld - CTRL.motor->Lq; // for IPMSM
    // mech
    CTRL.motor->npp     = MOTOR_NUMBER_OF_POLE_PAIRS;
    CTRL.motor->npp_inv = 1.0 / CTRL.motor->npp;
    CTRL.motor->Js      = MOTOR_SHAFT_INERTIA * (1.0+LOAD_INERTIA);
    CTRL.motor->Js_inv  = 1.0 / CTRL.motor->Js;

    // PID调谐
    ACMSIMC_PIDTuner();

    // PID regulators
    CTRL.S->iM  = &pid1_iM;
    CTRL.S->iT  = &pid1_iT;
    CTRL.S->pos = &pid1_pos;
    CTRL.S->spd = &pid1_spd;
}

/* id=0控制 */
extern int Set_current_loop;
void null_d_control(REAL set_iq_cmd, REAL set_id_cmd){
    // 定义局部变量，减少对CTRL的直接调用
    #define MOTOR  (*CTRL.motor)

    /// 4. 帕克变换（使用反馈位置）
    CTRL.S->cosT = cos(CTRL.I->theta_d_elec);
    CTRL.S->sinT = sin(CTRL.I->theta_d_elec);
    CTRL.I->idq[0] = AB2M(CTRL.I->iab[0], CTRL.I->iab[1], CTRL.S->cosT, CTRL.S->sinT);
    CTRL.I->idq[1] = AB2T(CTRL.I->iab[0], CTRL.I->iab[1], CTRL.S->cosT, CTRL.S->sinT);

    /// 5. 转速环（使用反馈转速）
    if(CTRL.S->vc_count++ == SPEED_LOOP_CEILING){
        CTRL.S->vc_count = 1;

        pid1_spd.Ref = CTRL.I->cmd_speed_rpm*RPM_2_ELEC_RAD_PER_SEC;
        pid1_spd.Fbk = CTRL.I->omg_elec;
        pid1_spd.calc(&pid1_spd);

        CTRL.I->idq_cmd[1] = pid1_spd.Out;
    }
    // 磁链环
    #if CONTROL_STRATEGY == NULL_D_AXIS_CURRENT_CONTROL
        // CTRL.I->cmd_rotor_flux_Wb = 0.0;
        // CTRL.I->idq_cmd[0] = CTRL.I->cmd_rotor_flux_Wb / MOTOR.Ld;

        CTRL.I->idq_cmd[0] = set_id_cmd;
    #else
        CTRL.I->cmd_rotor_flux_Wb = MOTOR.Ld * set_id_cmd;
        CTRL.I->idq_cmd[0] = set_id_cmd;
        #if PC_SIMULATION
            printf("CONTROL_STRATEGY Not Implemented: %s", CONTROL_STRATEGY);getch();
        #endif
    #endif
    // 转矩 For luenberger position observer for HFSI
    CTRL.I->Tem     = MOTOR.npp * (MOTOR.KE*CTRL.I->idq[1]     + (MOTOR.Ld-MOTOR.Lq)*CTRL.I->idq[0]    *CTRL.I->idq[1]);
    CTRL.I->Tem_cmd = MOTOR.npp * (MOTOR.KE*CTRL.I->idq_cmd[1] + (MOTOR.Ld-MOTOR.Lq)*CTRL.I->idq_cmd[0]*CTRL.I->idq_cmd[1]);

    /// 5.Sweep 扫频将覆盖上面产生的励磁、转矩电流指令
    #if EXCITATION_TYPE == EXCITATION_SWEEP_FREQUENCY
        #if SWEEP_FREQ_C2V == TRUE
            pid1_iq.Ref = set_iq_cmd; 
        #endif
        #if SWEEP_FREQ_C2C == TRUE
            pid1_iq.Ref = 0.0;
            pid1_id.Ref = set_iq_cmd; // 故意反的
        #endif
    #endif

    // debug
    // Set_current_loop = 1;
    // set_iq_cmd = 1;

    /// 6. 电流环
    // d-axis
    pid1_id.Fbk = CTRL.I->idq[0];
    pid1_id.Ref = CTRL.I->idq_cmd[0];
    pid1_id.calc(&pid1_id);
    // q-axis
    pid1_iq.Fbk = CTRL.I->idq[1];
    pid1_iq.Ref = CTRL.I->idq_cmd[1]; if(Set_current_loop==1){pid1_iq.Ref = set_iq_cmd;}
    pid1_iq.calc(&pid1_iq);
    // 电压电流解耦
    #if VOLTAGE_CURRENT_DECOUPLING_CIRCUIT == TRUE
        REAL decoupled_d_axis_voltage = pid1_id.Out -             pid1_iq.Fbk*MOTOR.Lq *CTRL.I->omg_elec;
        REAL decoupled_q_axis_voltage = pid1_iq.Out + ( MOTOR.KE + pid1_id.Fbk*MOTOR.Ld)*CTRL.I->omg_elec;
    #else
        REAL decoupled_d_axis_voltage = pid1_id.Out;
        REAL decoupled_q_axis_voltage = pid1_iq.Out;
    #endif
    CTRL.O->udq_cmd[0] = decoupled_d_axis_voltage;
    CTRL.O->udq_cmd[1] = decoupled_q_axis_voltage;

    /// 7. 反帕克变换
    CTRL.O->uab_cmd[0] = MT2A(CTRL.O->udq_cmd[0], CTRL.O->udq_cmd[1], CTRL.S->cosT, CTRL.S->sinT);
    CTRL.O->uab_cmd[1] = MT2B(CTRL.O->udq_cmd[0], CTRL.O->udq_cmd[1], CTRL.S->cosT, CTRL.S->sinT);
}



/* Initial Position Detection needs position update rate is at 1/CL_TS. */
void doIPD(){
    // 帕克变换（使用反馈位置）
    CTRL.S->cosT = cos(ENC.excitation_angle_deg/180.0*M_PI); // CTRL.I->theta_d_elec
    CTRL.S->sinT = sin(ENC.excitation_angle_deg/180.0*M_PI); // CTRL.I->theta_d_elec
    CTRL.I->idq[0] = AB2M(CTRL.I->iab[0], CTRL.I->iab[1], CTRL.S->cosT, CTRL.S->sinT);
    CTRL.I->idq[1] = AB2T(CTRL.I->iab[0], CTRL.I->iab[1], CTRL.S->cosT, CTRL.S->sinT);

    #define CURRENT_COMMAND_VALUE (0.5*MOTOR_RATED_CURRENT_RMS)

    /* 根据CL_TS周期更新的位置反馈信息去辨识一个 */
    static int number_of_excitation_times = 0;
    pid1_id.Ref += CL_TS * 3;
    /* Stage 1 */
    if(number_of_excitation_times==0){
        // if reached then reset
        if( pid1_id.Ref > CURRENT_COMMAND_VALUE ){
            pid1_id.Ref = 0.0;
            number_of_excitation_times = 1;
        }
        // identify the offset angle (fast)
        if(CTRL.I->theta_d_elec > CTRL.I->theta_d_elec_previous){
            ENC.excitation_angle_deg -= CL_TS*3600;
        }else if(CTRL.I->theta_d_elec < CTRL.I->theta_d_elec_previous){
            ENC.excitation_angle_deg += CL_TS*3600;
        }
    /* Stage 2 */
    }else if(number_of_excitation_times==1){
        // if reached then reset
        if( pid1_id.Ref > 2*CURRENT_COMMAND_VALUE ){
            pid1_id.Ref = 0.0;
            number_of_excitation_times = 2;
        }
        // identify the offset angle (medium fast)
        if(CTRL.I->theta_d_elec > CTRL.I->theta_d_elec_previous){
            ENC.excitation_angle_deg -= CL_TS*300;
        }else if(CTRL.I->theta_d_elec < CTRL.I->theta_d_elec_previous){
            ENC.excitation_angle_deg += CL_TS*300;
        }
    /* Stage 3 */
    }else if(number_of_excitation_times==2){
        pid1_id.Ref = CURRENT_COMMAND_VALUE;
        // identify the offset angle (slow)
        if(CTRL.I->theta_d_elec > CTRL.I->theta_d_elec_previous){
            ENC.excitation_angle_deg -= CL_TS*100;
        }else if(CTRL.I->theta_d_elec < CTRL.I->theta_d_elec_previous){
            ENC.excitation_angle_deg += CL_TS*100;
        }
        static Uint32 count=0;
        if(count++>5000){
            number_of_excitation_times = 3;
        }
    /* Stage 4 */
    }else{
        pid1_id.Ref = CURRENT_COMMAND_VALUE;
        static Uint32 count=0;
        if(count++>2000){
            ENC.theta_d_offset = -(CTRL.I->theta_d_elec - ENC.excitation_angle_deg/180.0*M_PI);
            CTRL.S->IPD_Done = TRUE;
            pid1_id.I_Term = 0.0; //
            pid1_iq.I_Term = 0.0; //
            CTRL.O->uab_cmd[0] = 0.0;
            CTRL.O->uab_cmd[1] = 0.0;
            return;
        }
    }

    pid1_id.Fbk = CTRL.I->idq[0];
    pid1_id.calc(&pid1_id);
    CTRL.O->uab_cmd[0] = MT2A(pid1_id.Out, 0.0, CTRL.S->cosT, CTRL.S->sinT);
    CTRL.O->uab_cmd[1] = MT2B(pid1_id.Out, 0.0, CTRL.S->cosT, CTRL.S->sinT);
}

/* Phase Sequence Detection */
void doPSD(){
    // 帕克变换（使用反馈位置）
    CTRL.S->cosT = cos(PSD.theta_excitation); // CTRL.I->theta_d_elec
    CTRL.S->sinT = sin(PSD.theta_excitation); // CTRL.I->theta_d_elec
    CTRL.I->idq[0] = AB2M(CTRL.I->iab[0], CTRL.I->iab[1], CTRL.S->cosT, CTRL.S->sinT);
    CTRL.I->idq[1] = AB2T(CTRL.I->iab[0], CTRL.I->iab[1], CTRL.S->cosT, CTRL.S->sinT);

    #define CURRENT_COMMAND_VALUE (0.5*MOTOR_RATED_CURRENT_RMS)

    pid1_id.Ref = CURRENT_COMMAND_VALUE;
    pid1_id.Fbk = CTRL.I->idq[0];
    pid1_id.calc(&pid1_id);
    CTRL.O->uab_cmd[0] = MT2A(pid1_id.Out, 0.0, CTRL.S->cosT, CTRL.S->sinT);
    CTRL.O->uab_cmd[1] = MT2B(pid1_id.Out, 0.0, CTRL.S->cosT, CTRL.S->sinT);
    CTRL.O->uab_cmd_to_inverter[0] = CTRL.O->uab_cmd[0];
    CTRL.O->uab_cmd_to_inverter[1] = CTRL.O->uab_cmd[1];

    if(CTRL.timebase>2.0){
        CTRL.S->PSD_Done = TRUE;
        PSD.theta_excitation = 0.0;
        PSD.theta_d_elec_entered = 0.0;
        PSD.countEntered = 0;
    }else if(CTRL.timebase>1.75){ // EXCITE_BETA_AXIS==TRUE: 1.75 else 1.5
        // Reverse the rotation of current vector
        PSD.theta_excitation -= CL_TS*1.0*M_PI; // EXCITE_BETA_AXIS==TRUE: -CL_TS* else CL_TS*
    }else if(CTRL.timebase>1){
        // Rotate current vector
        PSD.theta_excitation += CL_TS*1.0*M_PI; // EXCITE_BETA_AXIS==TRUE: -CL_TS* else CL_TS*

        // 第一次进来
        if(PSD.countEntered++==0){
            // 以当前位置角为d轴位置进行初始位置补偿。
            ENC.theta_d_offset = - ENC.theta_d__state;
            PSD.theta_d_elec_entered = 0.0;
        }

        // 转到中途的时候，在开始反转之前，赶快判断一下旋转的方向
        if(fabs(CTRL.timebase-1.49)<CL_TS){
            if( CTRL.I->theta_d_elec > PSD.theta_d_elec_entered)
                PSD.direction = 1;
            else{
                PSD.direction = -1;
            }
        }
    }else{
        // Excite constant current vector and wait for alignment
    }
}

void commissioning(){
    /* PSD */
    if(CTRL.S->PSD_Done == FALSE){
        doPSD();
        return;
    }

    /* IPD */
    // if(CTRL.S->IPD_Done == FALSE){
    //     doIPD();
    //     inverter_voltage_command();
    //     return;
    // }

    /* SC */
    // 帕克变换（使用反馈位置）
    CTRL.S->cosT = cos(CTRL.I->theta_d_elec); // CTRL.I->theta_d_elec
    CTRL.S->sinT = sin(CTRL.I->theta_d_elec); // CTRL.I->theta_d_elec
    CTRL.I->idq[0] = AB2M(CTRL.I->iab[0], CTRL.I->iab[1], CTRL.S->cosT, CTRL.S->sinT);
    CTRL.I->idq[1] = AB2T(CTRL.I->iab[0], CTRL.I->iab[1], CTRL.S->cosT, CTRL.S->sinT);
    // 参数自整定
    StepByStepCommissioning();
    inverter_voltage_command();
}

#if PC_SIMULATION
    REAL Set_maunal_current_id=0.0;
#else
    extern float Set_maunal_current_id;
#endif
void controller(REAL set_rpm_speed_command, REAL set_iq_cmd, REAL set_id_cmd){

    /// 0. 参数时变
    // if (fabs(CTRL.timebase-2.0)<CL_TS){
    //     printf("[Runtime] Rotor resistance of the simulated IM has changed!\n");
    //     ACM.alpha = 0.5*IM_ROTOR_RESISTANCE / IM_MAGNETIZING_INDUCTANCE;
    //     ACM.rreq = ACM.alpha*ACM.Lmu;
    //     ACM.rr   = ACM.alpha*(ACM.Lm+ACM.Llr);
    // }

    /// 1. 生成转速指令
    CTRL.I->cmd_position_rad    = 0.0;  // mechanical
    CTRL.I->cmd_speed_rpm       = set_rpm_speed_command;     // mechanical

    /// 2. 生成磁链指令
    if(Set_maunal_current_id==0.0){
        // if(set_iq_cmd<1){
        //     set_id_cmd = 1; // 2;
        // }
    }

    /// 3. 调用观测器：估计的电气转子位置和电气转子转速反馈
    pmsm_observers();
    if(CTRL.S->go_sensorless == TRUE){
        //（无感）
        CTRL.I->omg_elec     = ELECTRICAL_SPEED_FEEDBACK;    //harnefors.omg_elec;
        CTRL.I->theta_d_elec = ELECTRICAL_POSITION_FEEDBACK; //harnefors.theta_d;        
    }

    /// 调用具体的控制器
    #if CONTROL_STRATEGY == NULL_D_AXIS_CURRENT_CONTROL
        null_d_control(set_iq_cmd, set_id_cmd);
    #endif

    /// 8. 补偿逆变器非线性
    inverter_voltage_command();

    /// 9. 结束
    #if PC_SIMULATION
        // for plot
        ACM.rpm_cmd = set_rpm_speed_command;
        // CTRL.speed_ctrl_err = set_rpm_speed_command*RPM_2_ELEC_RAD_PER_SEC - CTRL.I->omg_elec;
    #endif
}






/* 逆变器非线性 */

/* 拟合法 */
REAL sigmoid(REAL x){
    // beta
    // REAL a1 = 0.0; // *1.28430164
    // REAL a2 = 8.99347107;
    // REAL a3 = 5.37783655;

    // -------------------------------

    // b-phase @20 V
    // REAL a1 = 0.0; //1.25991178;
    // REAL a2 = 2.30505904;
    // REAL a3 = 12.93721814;

    // b-phase @80 V
    REAL a1 = 0.0; //1.28430164;
    REAL a2 = 7.78857441;
    REAL a3 = 6.20979077;

    // b-phase @180 V
    // REAL a1 = 0.0; //1.50469916;
    // REAL a2 = 13.48467604;
    // REAL a3 = 5.22150403;

    return a1 * x + a2 / (1.0 + exp(-a3 * x)) - a2*0.5;
}

void inverter_voltage_command(){
    #if INVERTER_NONLINEARITY_COMPENSATION

        #if FALSE
            /* 查表法-补偿 */
            // // Measured in Simulation when the inverter is modelled according to the experimental measurements
            // #define LENGTH_OF_LUT  19
            // REAL lut_current_ctrl[LENGTH_OF_LUT] = {-3.78, -3.36, -2.94, -2.52, -2.1, -1.68, -1.26, -0.839998, -0.419998, -0, 0.420002, 0.840002, 1.26, 1.68, 2.1, 2.52, 2.94, 3.36, 3.78};
            // // REAL lut_voltage_ctrl[LENGTH_OF_LUT] = {-6.41808, -6.41942, -6.39433, -6.36032, -6.25784, -6.12639, -5.79563, -5.35301, -3.61951, 0, 3.5038, 5.24969, 5.73176, 6.11153, 6.24738, 6.35941, 6.43225, 6.39274, 6.39482};
            // #define MANUAL_CORRECTION 1.1 // [V]
            // REAL lut_voltage_ctrl[LENGTH_OF_LUT] = {
            //     -6.41808+MANUAL_CORRECTION, 
            //     -6.41942+MANUAL_CORRECTION, 
            //     -6.39433+MANUAL_CORRECTION, 
            //     -6.36032+MANUAL_CORRECTION, 
            //     -6.25784+MANUAL_CORRECTION, 
            //     -6.12639+MANUAL_CORRECTION, 
            //     -5.79563+MANUAL_CORRECTION, 
            //     -5.35301+MANUAL_CORRECTION, 
            //     -3.61951, 0, 3.5038,
            //      5.24969-MANUAL_CORRECTION,
            //      5.73176-MANUAL_CORRECTION,
            //      6.11153-MANUAL_CORRECTION,
            //      6.24738-MANUAL_CORRECTION,
            //      6.35941-MANUAL_CORRECTION,
            //      6.43225-MANUAL_CORRECTION,
            //      6.39274-MANUAL_CORRECTION,
            //      6.39482-MANUAL_CORRECTION};

            // #define LENGTH_OF_LUT  100
            // REAL lut_current_ctrl[LENGTH_OF_LUT] = {-4.116, -4.032, -3.948, -3.864, -3.78, -3.696, -3.612, -3.528, -3.444, -3.36, -3.276, -3.192, -3.108, -3.024, -2.94, -2.856, -2.772, -2.688, -2.604, -2.52, -2.436, -2.352, -2.268, -2.184, -2.1, -2.016, -1.932, -1.848, -1.764, -1.68, -1.596, -1.512, -1.428, -1.344, -1.26, -1.176, -1.092, -1.008, -0.923998, -0.839998, -0.755998, -0.671998, -0.587998, -0.503998, -0.419998, -0.335999, -0.251999, -0.168, -0.084, -0, 0.084, 0.168, 0.252001, 0.336001, 0.420002, 0.504002, 0.588002, 0.672002, 0.756002, 0.840002, 0.924002, 1.008, 1.092, 1.176, 1.26, 1.344, 1.428, 1.512, 1.596, 1.68, 1.764, 1.848, 1.932, 2.016, 2.1, 2.184, 2.268, 2.352, 2.436, 2.52, 2.604, 2.688, 2.772, 2.856, 2.94, 3.024, 3.108, 3.192, 3.276, 3.36, 3.444, 3.528, 3.612, 3.696, 3.78, 3.864, 3.948, 4.032, 4.116, 4.2};
            // REAL lut_voltage_ctrl[LENGTH_OF_LUT] = {-6.48905, -6.49021, -6.49137, -6.49253, -6.49368, -6.49227, -6.49086, -6.48945, -6.48803, -6.48662, -6.47993, -6.47323, -6.46653, -6.45983, -6.45313, -6.44465, -6.43617, -6.42769, -6.41921, -6.41072, -6.38854, -6.36637, -6.34419, -6.32202, -6.29984, -6.27187, -6.2439, -6.21593, -6.18796, -6.15999, -6.09216, -6.02433, -5.9565, -5.88867, -5.82083, -5.73067, -5.6405, -5.55033, -5.46016, -5.36981, -5.02143, -4.67305, -4.32467, -3.97629, -3.62791, -2.90251, -2.17689, -1.45126, -0.725632, -1e-06, 0.702441, 1.40488, 2.10732, 2.80976, 3.5122, 3.86321, 4.21409, 4.56497, 4.91585, 5.26649, 5.36459, 5.46268, 5.56078, 5.65887, 5.75696, 5.8346, 5.91224, 5.98987, 6.0675, 6.14513, 6.17402, 6.20286, 6.2317, 6.26054, 6.28938, 6.31347, 6.33755, 6.36164, 6.38572, 6.40981, 6.4303, 6.4508, 6.47129, 6.49178, 6.49105, 6.48483, 6.4786, 6.47238, 6.46616, 6.45994, 6.46204, 6.46413, 6.46623, 6.46832, 6.47042, 6.47202, 6.47363, 6.47524, 6.47684, 6.47843};

            // Experimental measurements
            // #define LENGTH_OF_LUT 21
            // REAL lut_current_ctrl[LENGTH_OF_LUT] = {-4.19999, -3.77999, -3.36001, -2.94002, -2.51999, -2.10004, -1.68004, -1.26002, -0.840052, -0.419948, 5.88754e-06, 0.420032, 0.839998, 1.26003, 1.67998, 2.10009, 2.51996, 2.87326, 3.36001, 3.78002, 4.2};
            // REAL lut_voltage_ctrl[LENGTH_OF_LUT] = {-5.20719, -5.2079, -5.18934, -5.15954, -5.11637, -5.04723, -4.93463, -4.76367, -4.42522, -3.46825, 0.317444, 3.75588, 4.55737, 4.87773, 5.04459, 5.15468, 5.22904, 5.33942, 5.25929, 5.28171, 5.30045};

            // Experimental measurements 03-18-2021
            #define LENGTH_OF_LUT  41
            REAL lut_current_ctrl[LENGTH_OF_LUT] = {-4.20001, -3.98998, -3.78002, -3.57, -3.36002, -3.14999, -2.93996, -2.72993, -2.51998, -2.31003, -2.1, -1.88996, -1.67999, -1.46998, -1.25999, -1.05001, -0.839962, -0.62995, -0.420046, -0.210048, 1.39409e-05, 0.209888, 0.420001, 0.629998, 0.840008, 1.05002, 1.25999, 1.47002, 1.68001, 1.89001, 2.10002, 2.31, 2.51999, 2.73004, 2.94, 3.14996, 3.35995, 3.57001, 3.77999, 3.98998, 4.2};
            REAL lut_voltage_ctrl[LENGTH_OF_LUT] = {-5.75434, -5.74721, -5.72803, -5.70736, -5.68605, -5.66224, -5.63274, -5.59982, -5.56391, -5.52287, -5.47247, -5.40911, -5.33464, -5.25019, -5.14551, -5.00196, -4.80021, -4.48369, -3.90965, -2.47845, -0.382101, 2.02274, 3.7011, 4.35633, 4.71427, 4.94376, 5.10356, 5.22256, 5.31722, 5.39868, 5.46753, 5.5286, 5.57507, 5.62385, 5.66235, 5.70198, 5.73617, 5.76636, 5.79075, 5.81737, 5.83632};

            REAL ualbe_dist[2];
            get_distorted_voltage_via_LUT( CTRL.O->uab_cmd[0], CTRL.O->uab_cmd[1], CTRL.I->iab[0], CTRL.I->iab[1], ualbe_dist, lut_voltage_ctrl, lut_current_ctrl, LENGTH_OF_LUT);
            CTRL.O->uab_cmd_to_inverter[0] = CTRL.O->uab_cmd[0] + ualbe_dist[0];
            CTRL.O->uab_cmd_to_inverter[1] = CTRL.O->uab_cmd[1] + ualbe_dist[1];
        #else
            /* 拟合法-补偿 */
            REAL ualbe_dist[2] = {0.0, 0.0};
            get_distorted_voltage_via_CurveFitting( CTRL.O->uab_cmd[0], CTRL.O->uab_cmd[1], CTRL.I->iab[0], CTRL.I->iab[1], ualbe_dist);
            CTRL.O->uab_cmd_to_inverter[0] = CTRL.O->uab_cmd[0] + ualbe_dist[0];
            CTRL.O->uab_cmd_to_inverter[1] = CTRL.O->uab_cmd[1] + ualbe_dist[1];
        #endif

        /* 梯形波自适应 */
        Modified_ParkSul_Compensation();
        // not tested yet

    #else
        CTRL.O->uab_cmd_to_inverter[0] = CTRL.O->uab_cmd[0];
        CTRL.O->uab_cmd_to_inverter[1] = CTRL.O->uab_cmd[1];            
    #endif
}

/* 查表法 */
REAL look_up_phase_current(REAL current, REAL *lut_voltage, REAL *lut_current, int length_of_lut){
    /* assume lut_voltage[0] is negative and lut_voltage[-1] is positive */
    int j;
    if( current<lut_current[0] ){
        return lut_voltage[0];
    }else if( current>lut_current[length_of_lut-1] ){
        return lut_voltage[length_of_lut-1];
    }else{
        for(j=0;j<length_of_lut-1;++j){
            if( current>lut_current[j] && current<lut_current[j+1] ){
                REAL slope = (lut_voltage[j+1] - lut_voltage[j]) / (lut_current[j+1] - lut_current[j]);
                return (current - lut_current[j]) * slope + lut_voltage[j];
                // return 0.5*(lut_voltage[j]+lut_voltage[j+1]); // this is just wrong! stupid!
            }
        }
    }
    #if PC_SIMULATION
        printf("DEBUG inverter.c. %g, %d, %d\n", current, j, length_of_lut);
    #endif
    return 0.0;
}
REAL trapezoidal_voltage_by_phase_current(REAL current, REAL Vsat, REAL theta_trapezoidal){
    theta_trapezoidal;
    return 0.0;
}
void get_distorted_voltage_via_CurveFitting(REAL ual, REAL ube, REAL ial, REAL ibe, REAL *ualbe_dist){
    // The data are measured in stator resistance identification with amplitude invariant d-axis current and d-axis voltage.

    /* 拟合法 */
    REAL ia,ib,ic;
    ia = 1 * (       ial                              );
    ib = 1 * (-0.5 * ial - SIN_DASH_2PI_SLASH_3 * ibe );
    ic = 1 * (-0.5 * ial - SIN_2PI_SLASH_3      * ibe );

    REAL dist_ua = sigmoid(ia);
    REAL dist_ub = sigmoid(ib);
    REAL dist_uc = sigmoid(ic);

    // Clarke transformation（三分之二，0.5倍根号三）
    ualbe_dist[0] = 0.66666667 * (dist_ua - 0.5*dist_ub - 0.5*dist_uc);
    ualbe_dist[1] = 0.66666667 * 0.8660254 * ( dist_ub -     dist_uc); // 0.5773502695534

    // for plot
    INV.ia = ia;
    INV.ib = ib;
    INV.ic = ic;
    INV.dist_ua = dist_ua;
    INV.dist_ub = dist_ub;
    INV.dist_uc = dist_uc;
}
void get_distorted_voltage_via_LUT(REAL ual, REAL ube, REAL ial, REAL ibe, REAL *ualbe_dist, REAL *lut_voltage, REAL *lut_current, int length_of_lut){
    // The data are measured in stator resistance identification with amplitude invariant d-axis current and d-axis voltage.

    /* 查表法 */
    if(TRUE){
        REAL ia,ib,ic;
        ia = 1 * (       ial                              );
        ib = 1 * (-0.5 * ial - SIN_DASH_2PI_SLASH_3 * ibe );
        ic = 1 * (-0.5 * ial - SIN_2PI_SLASH_3      * ibe );

        REAL dist_ua = look_up_phase_current(ia, lut_voltage, lut_current, length_of_lut);
        REAL dist_ub = look_up_phase_current(ib, lut_voltage, lut_current, length_of_lut);
        REAL dist_uc = look_up_phase_current(ic, lut_voltage, lut_current, length_of_lut);

        // Clarke transformation（三分之二，0.5倍根号三）
        ualbe_dist[0] = 0.66666667 * (dist_ua - 0.5*dist_ub - 0.5*dist_uc);
        ualbe_dist[1] = 0.66666667 * 0.8660254 * ( dist_ub -     dist_uc); // 0.5773502695534
    }else{
        /* AB2U_AI 这宏假设了零序分量为零，即ia+ib+ic=0，但是电压并不一定满足吧，所以还是得用上面的？ */
        REAL ia,ib,ic;
        ia = AB2U_AI(ial, ibe); // ia = 1 * (       ial                              );
        ib = AB2V_AI(ial, ibe); // ib = 1 * (-0.5 * ial - SIN_DASH_2PI_SLASH_3 * ibe );
        // ic = AB2W_AI(ial, ibe); // ic = 1 * (-0.5 * ial - SIN_2PI_SLASH_3      * ibe );

        REAL dist_ua = look_up_phase_current(ia, lut_voltage, lut_current, length_of_lut);
        REAL dist_ub = look_up_phase_current(ib, lut_voltage, lut_current, length_of_lut);
        // REAL dist_uc = look_up_phase_current(ic, lut_voltage, lut_current, length_of_lut);

        // Clarke transformation（三分之二，0.5倍根号三）
        // ualbe_dist[0] = 0.66666667 * (dist_ua - 0.5*dist_ub - 0.5*dist_uc);
        // ualbe_dist[1] = 0.66666667 * 0.8660254 * ( dist_ub -     dist_uc); // 0.5773502695534

        ualbe_dist[0] = UV2A_AI(dist_ua, dist_ub); // 0.66666667 * (dist_ua - 0.5*dist_ub - 0.5*dist_uc);
        ualbe_dist[1] = UV2B_AI(dist_ua, dist_ub); // 0.66666667 * 0.8660254 * ( dist_ub -     dist_uc);        
    }
}

/* ParkSul2012 梯形波 */
void inverterNonlinearity_Initialization(){
    INV.gamma_theta_trapezoidal = GAIN_THETA_TRAPEZOIDAL;
    INV.Vsat = 5.7246;

    INV.thetaA=0;
    INV.cos_thetaA=1;
    INV.sin_thetaA=0;

    // --
    INV.u_comp[0]=0;
    INV.u_comp[1]=0;
    INV.u_comp[2]=0;
    INV.ual_comp=0;
    INV.ube_comp=0;
    INV.uDcomp_atA=0;
    INV.uQcomp_atA=0;

    INV.iD_atA=0;
    INV.iQ_atA=0;
    INV.I5_plus_I7=0;
    INV.I5_plus_I7_LPF=0.0, 
    INV.theta_trapezoidal=11.0; // deg
}
REAL u_comp_per_phase(REAL Vsat, REAL thetaA, REAL theta_trapezoidal, REAL oneOver_theta_trapezoidal){
    REAL compensation;

    // if(thetaA>0){
    //         compensation = Vsat;
    // }else{ // thetaA < 0
    //         compensation = -Vsat;
    // }

    if(thetaA>0){
        if(thetaA<theta_trapezoidal){
            compensation = thetaA * Vsat * oneOver_theta_trapezoidal;
        }else if(thetaA>M_PI - theta_trapezoidal){
            compensation = (M_PI-thetaA) * Vsat * oneOver_theta_trapezoidal;
        }else{
            compensation = Vsat;
        }
    }else{ // thetaA < 0
        if(-thetaA<theta_trapezoidal){
            compensation = -thetaA * -Vsat * oneOver_theta_trapezoidal;
        }else if(-thetaA>M_PI - theta_trapezoidal){
            compensation = (M_PI+thetaA) * -Vsat * oneOver_theta_trapezoidal;
        }else{
            compensation = -Vsat;
        }
    }

    return compensation;
}
REAL lpf1_inverter(REAL x, REAL y_tminus1){
    // #define LPF1_RC 0.6 // 0.7, 0.8, 1.2, 3太大滤得过分了 /* 观察I5+I7_LPF的动态情况进行确定 */
    // #define ALPHA_RC (TS/(LPF1_RC + TS)) // TODO：优化
    // return y_tminus1 + ALPHA_RC * (x - y_tminus1); // 0.00020828993959591752
    return y_tminus1 + 0.00020828993959591752 * (x - y_tminus1);
}
REAL shift2pi(REAL thetaA){
    if(thetaA>M_PI){
        return thetaA - 2*M_PI;
    }else if(thetaA<-M_PI){
        return thetaA + 2*M_PI;
    }else{
        return thetaA;
    }
}
void Modified_ParkSul_Compensation(void){

    /* Park12/14
     * */
    // Phase A current's fundamental component transformation    
    INV.thetaA = -M_PI*1.5 + CTRL.I->theta_d_elec + atan2(CTRL.I->idq_cmd[1], CTRL.I->idq_cmd[0]); /* Q: why -pi*(1.5)? */ /* ParkSul suggests to use PLL to extract thetaA from ia(t)*/
    INV.thetaA = shift2pi(INV.thetaA); /* Q: how to handle it when INV.thetaA jumps between pi and -pi? */ // 这句话绝对不能省去，否则C相的梯形波会出错。

    INV.cos_thetaA = cos(INV.thetaA);
    INV.sin_thetaA = sin(INV.thetaA);
    INV.iD_atA = AB2M(IS_C(0),IS_C(1), INV.cos_thetaA, INV.sin_thetaA);
    INV.iQ_atA = AB2T(IS_C(0),IS_C(1), INV.cos_thetaA, INV.sin_thetaA);

    INV.I5_plus_I7 = INV.iD_atA * sin(6*INV.thetaA); /* Q: Why sin? Why not cos? 和上面的-1.5*pi有关系吗？ */
    INV.I5_plus_I7_LPF = lpf1_inverter(INV.I5_plus_I7, INV.I5_plus_I7_LPF); /* lpf1 for inverter */

    // The adjusting of theta_t via 6th harmonic magnitude
    INV.theta_trapezoidal += CL_TS*INV.gamma_theta_trapezoidal*INV.I5_plus_I7_LPF;

    // 两种方法选其一：第一种可用于sensorless系统。
    if(FALSE){
        /* 利用theta_t的饱和特性辨识Vsat（次优解） */
    }else{
        // 由于实际逆变器有一个调制比低引入5、7次谐波的问题，所以最佳theta_t不在11度附近了。
        if(INV.theta_trapezoidal>=17*M_PI_OVER_180){
            INV.theta_trapezoidal = 17*M_PI_OVER_180;
        }
        if(INV.theta_trapezoidal<=0){
            INV.theta_trapezoidal = 0;
        }
    }

    REAL oneOver_theta_trapezoidal = 1.0/INV.theta_trapezoidal;
    INV.u_comp[0] = u_comp_per_phase(INV.Vsat, INV.thetaA, INV.theta_trapezoidal, oneOver_theta_trapezoidal);
    INV.u_comp[1] = u_comp_per_phase(INV.Vsat, shift2pi(INV.thetaA-TWO_PI_OVER_3), INV.theta_trapezoidal, oneOver_theta_trapezoidal);
    INV.u_comp[2] = u_comp_per_phase(INV.Vsat, shift2pi(INV.thetaA-2*TWO_PI_OVER_3), INV.theta_trapezoidal, oneOver_theta_trapezoidal);

    // TODO: 改成恒幅值变换
    INV.ual_comp = SQRT_2_SLASH_3      * (INV.u_comp[0] - 0.5*INV.u_comp[1] - 0.5*INV.u_comp[2]); // sqrt(2/3.)
    INV.ube_comp = 0.70710678118654746 * (                    INV.u_comp[1] -     INV.u_comp[2]); // sqrt(2/3.)*sin(2*pi/3) = sqrt(2/3.)*(sqrt(3)/2)

    // 区分补偿前的电压和补偿后的电压：
    // CTRL.ual, CTRL.ube 是补偿前的电压！
    // CTRL.ual + INV.ual_comp, CTRL.ube + INV.ube_comp 是补偿后的电压！

    // for plotting
    INV.uDcomp_atA = AB2M(INV.ual_comp, INV.ube_comp, INV.cos_thetaA, INV.sin_thetaA);
    INV.uQcomp_atA = AB2T(INV.ual_comp, INV.ube_comp, INV.cos_thetaA, INV.sin_thetaA);
}

#endif