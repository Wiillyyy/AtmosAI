################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_cacheaxi.c \
../Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c.c \
../Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c_ex.c 

OBJS += \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_cacheaxi.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c.o \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c_ex.o 

C_DEPS += \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_cacheaxi.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c.d \
./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c_ex.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/STM32N6xx_HAL_Driver/%.o Drivers/STM32N6xx_HAL_Driver/%.su Drivers/STM32N6xx_HAL_Driver/%.cyclo: ../Drivers/STM32N6xx_HAL_Driver/%.c Drivers/STM32N6xx_HAL_Driver/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DNX_INCLUDE_USER_DEFINE_FILE -DTX_INCLUDE_USER_DEFINE_FILE -DTX_SINGLE_MODE_SECURE=1 -DLL_ATON_PLATFORM=12 -DLL_ATON_OSAL=4 -c -I../../../FSBL/NetXDuo/App -I../../../Middlewares/ST/AI/Npu/ll_aton -I../../../Middlewares/ST/AI/Npu/Devices/STM32N6XX -I../../../Middlewares/ST/AI/Inc -I../../../Middlewares/ST/AI/Npu/Inc -I../../../FSBL/X-CUBE-AI/App -I../../../Drivers/BSP/Components/lan8742 -I../../../FSBL/NetXDuo/Target -I../../../FSBL/Core/Inc -I../../../FSBL/AZURE_RTOS/App -I../../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../../Drivers/BSP/Components/lan8742 -I../../../Middlewares/ST/netxduo/addons/dhcp -I../../../Middlewares/ST/netxduo/tsn/inc -I../../../Middlewares/ST/netxduo/common/drivers/ethernet -I../../../Middlewares/ST/threadx/common/inc -I../../../Middlewares/ST/netxduo/common/inc -I../../../Middlewares/ST/netxduo/ports/cortex_m55/gnu/inc -I../../../Middlewares/ST/threadx/ports/cortex_m55/gnu/inc -I../../../Drivers/CMSIS/Include -I"C:/Users/William/Documents/GitHub/AtmosAI_ETRS606/STM32CubeIDE/FSBL/Drivers/STM32N6xx_HAL_Driver" -I"C:/Users/William/Documents/GitHub/AtmosAI_ETRS606/STM32CubeIDE/FSBL/Drivers/STMems" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-STM32N6xx_HAL_Driver

clean-Drivers-2f-STM32N6xx_HAL_Driver:
	-$(RM) ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_cacheaxi.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_cacheaxi.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_cacheaxi.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_cacheaxi.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c.su ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c_ex.cyclo ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c_ex.d ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c_ex.o ./Drivers/STM32N6xx_HAL_Driver/stm32n6xx_hal_i2c_ex.su

.PHONY: clean-Drivers-2f-STM32N6xx_HAL_Driver

