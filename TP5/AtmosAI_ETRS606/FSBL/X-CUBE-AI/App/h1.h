/**
  ******************************************************************************
  * @file    h1.h
  * @author  STEdgeAI
  * @date    2026-04-07 18:56:06
  * @brief   Minimal description of the generated c-implemention of the network
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  ******************************************************************************
  */
#ifndef LL_ATON_H1_H
#define LL_ATON_H1_H

#include "ll_aton_NN_interface.h"

/******************************************************************************/
#define LL_ATON_H1_C_MODEL_NAME        "h1"
#define LL_ATON_H1_ORIGIN_MODEL_NAME   "meteo_h1_model"

/************************** USER ALLOCATED IOs ********************************/
// No user allocated inputs
// No user allocated outputs

/************************** INPUTS ********************************************/
#define LL_ATON_H1_IN_NUM        (1)    // Total number of input buffers
// Input buffer 1 -- Input_0_out_0
#define LL_ATON_H1_IN_1_ALIGNMENT   (32)
#define LL_ATON_H1_IN_1_SIZE_BYTES  (52)

/************************** OUTPUTS *******************************************/
#define LL_ATON_H1_OUT_NUM        (1)    // Total number of output buffers
// Output buffer 1 -- Softmax_8_out_0
#define LL_ATON_H1_OUT_1_ALIGNMENT   (32)
#define LL_ATON_H1_OUT_1_SIZE_BYTES  (12)

/************************** FUNCTION DECLARATIONS *****************************/
LL_ATON_DECLARE_NAMED_NN_PROTOS(h1)

#endif /* LL_ATON_H1_H */
