################################################################################
# Manually added file for h1.c (X-CUBE-AI generated model)
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

C_SRCS += \
C:/Users/will/Documents/Cube/Nx_TCP_Echo_Client/FSBL/X-CUBE-AI/App/h1.c

OBJS += \
./Application/User/X-CUBE-AI/App/h1.o

C_DEPS += \
./Application/User/X-CUBE-AI/App/h1.d


# Each subdirectory must supply rules for building sources it contributes
Application/User/X-CUBE-AI/App/h1.o: C:/Users/will/Documents/Cube/Nx_TCP_Echo_Client/FSBL/X-CUBE-AI/App/h1.c Application/User/X-CUBE-AI/App/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DNX_INCLUDE_USER_DEFINE_FILE -DTX_INCLUDE_USER_DEFINE_FILE -DTX_SINGLE_MODE_SECURE=1 -DLL_ATON_PLATFORM=12 -DLL_ATON_OSAL=4 -c -I../../../FSBL/NetXDuo/App -I../../../Middlewares/ST/AI/Npu/ll_aton -I../../../Middlewares/ST/AI/Npu/Devices/STM32N6XX -I../../../Middlewares/ST/AI/Inc -I../../../Middlewares/ST/AI/Npu/Inc -I../../../FSBL/X-CUBE-AI/App -I../../../Drivers/BSP/Components/lan8742 -I../../../FSBL/NetXDuo/Target -I../../../FSBL/Core/Inc -I../../../FSBL/AZURE_RTOS/App -I../../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../../Drivers/BSP/Components/lan8742 -I../../../Middlewares/ST/netxduo/addons/dhcp -I../../../Middlewares/ST/netxduo/tsn/inc -I../../../Middlewares/ST/netxduo/common/drivers/ethernet -I../../../Middlewares/ST/threadx/common/inc -I../../../Middlewares/ST/netxduo/common/inc -I../../../Middlewares/ST/netxduo/ports/cortex_m55/gnu/inc -I../../../Middlewares/ST/threadx/ports/cortex_m55/gnu/inc -I../../../Drivers/CMSIS/Include -I"C:/Users/will/Documents/Cube/Nx_TCP_Echo_Client/STM32CubeIDE/FSBL/Drivers/STM32N6xx_HAL_Driver" -I"C:/Users/will/Documents/Cube/Nx_TCP_Echo_Client/STM32CubeIDE/FSBL/Drivers/STMems" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Application-2f-User-2f-X-2d-CUBE-2d-AI-2f-App

clean-Application-2f-User-2f-X-2d-CUBE-2d-AI-2f-App:
	-$(RM) ./Application/User/X-CUBE-AI/App/h1.cyclo ./Application/User/X-CUBE-AI/App/h1.d ./Application/User/X-CUBE-AI/App/h1.o ./Application/User/X-CUBE-AI/App/h1.su

.PHONY: clean-Application-2f-User-2f-X-2d-CUBE-2d-AI-2f-App
