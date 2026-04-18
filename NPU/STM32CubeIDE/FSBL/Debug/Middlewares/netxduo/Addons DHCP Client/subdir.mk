################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
C:/Users/will/Documents/Cube/Nx_TCP_Echo_Client/Middlewares/ST/netxduo/addons/dhcp/nxd_dhcp_client.c \
C:/Users/will/Documents/Cube/Nx_TCP_Echo_Client/Middlewares/ST/netxduo/addons/dhcp/nxd_dhcpv6_client.c 

OBJS += \
./Middlewares/netxduo/Addons\ DHCP\ Client/nxd_dhcp_client.o \
./Middlewares/netxduo/Addons\ DHCP\ Client/nxd_dhcpv6_client.o 

C_DEPS += \
./Middlewares/netxduo/Addons\ DHCP\ Client/nxd_dhcp_client.d \
./Middlewares/netxduo/Addons\ DHCP\ Client/nxd_dhcpv6_client.d 


# Each subdirectory must supply rules for building sources it contributes
Middlewares/netxduo/Addons\ DHCP\ Client/nxd_dhcp_client.o: C:/Users/will/Documents/Cube/Nx_TCP_Echo_Client/Middlewares/ST/netxduo/addons/dhcp/nxd_dhcp_client.c Middlewares/netxduo/Addons\ DHCP\ Client/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DNX_INCLUDE_USER_DEFINE_FILE -DTX_INCLUDE_USER_DEFINE_FILE -DTX_SINGLE_MODE_SECURE=1 -DLL_ATON_PLATFORM=12 -DLL_ATON_OSAL=4 -c -I../../../FSBL/NetXDuo/App -I../../../Middlewares/ST/AI/Npu/ll_aton -I../../../Middlewares/ST/AI/Npu/Devices/STM32N6XX -I../../../Middlewares/ST/AI/Inc -I../../../Middlewares/ST/AI/Npu/Inc -I../../../FSBL/X-CUBE-AI/App -I../../../Drivers/BSP/Components/lan8742 -I../../../FSBL/NetXDuo/Target -I../../../FSBL/Core/Inc -I../../../FSBL/AZURE_RTOS/App -I../../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../../Drivers/BSP/Components/lan8742 -I../../../Middlewares/ST/netxduo/addons/dhcp -I../../../Middlewares/ST/netxduo/tsn/inc -I../../../Middlewares/ST/netxduo/common/drivers/ethernet -I../../../Middlewares/ST/threadx/common/inc -I../../../Middlewares/ST/netxduo/common/inc -I../../../Middlewares/ST/netxduo/ports/cortex_m55/gnu/inc -I../../../Middlewares/ST/threadx/ports/cortex_m55/gnu/inc -I../../../Drivers/CMSIS/Include -I"C:/Users/will/Documents/Cube/Nx_TCP_Echo_Client/STM32CubeIDE/FSBL/Drivers/STM32N6xx_HAL_Driver" -I"C:/Users/will/Documents/Cube/Nx_TCP_Echo_Client/STM32CubeIDE/FSBL/Drivers/STMems" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"Middlewares/netxduo/Addons DHCP Client/nxd_dhcp_client.d" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"
Middlewares/netxduo/Addons\ DHCP\ Client/nxd_dhcpv6_client.o: C:/Users/will/Documents/Cube/Nx_TCP_Echo_Client/Middlewares/ST/netxduo/addons/dhcp/nxd_dhcpv6_client.c Middlewares/netxduo/Addons\ DHCP\ Client/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m55 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32N657xx -DNX_INCLUDE_USER_DEFINE_FILE -DTX_INCLUDE_USER_DEFINE_FILE -DTX_SINGLE_MODE_SECURE=1 -DLL_ATON_PLATFORM=12 -DLL_ATON_OSAL=4 -c -I../../../FSBL/NetXDuo/App -I../../../Middlewares/ST/AI/Npu/ll_aton -I../../../Middlewares/ST/AI/Npu/Devices/STM32N6XX -I../../../Middlewares/ST/AI/Inc -I../../../Middlewares/ST/AI/Npu/Inc -I../../../FSBL/X-CUBE-AI/App -I../../../Drivers/BSP/Components/lan8742 -I../../../FSBL/NetXDuo/Target -I../../../FSBL/Core/Inc -I../../../FSBL/AZURE_RTOS/App -I../../../Drivers/STM32N6xx_HAL_Driver/Inc -I../../../Drivers/CMSIS/Device/ST/STM32N6xx/Include -I../../../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy -I../../../Drivers/BSP/Components/lan8742 -I../../../Middlewares/ST/netxduo/addons/dhcp -I../../../Middlewares/ST/netxduo/tsn/inc -I../../../Middlewares/ST/netxduo/common/drivers/ethernet -I../../../Middlewares/ST/threadx/common/inc -I../../../Middlewares/ST/netxduo/common/inc -I../../../Middlewares/ST/netxduo/ports/cortex_m55/gnu/inc -I../../../Middlewares/ST/threadx/ports/cortex_m55/gnu/inc -I../../../Drivers/CMSIS/Include -I"C:/Users/will/Documents/Cube/Nx_TCP_Echo_Client/STM32CubeIDE/FSBL/Drivers/STM32N6xx_HAL_Driver" -I"C:/Users/will/Documents/Cube/Nx_TCP_Echo_Client/STM32CubeIDE/FSBL/Drivers/STMems" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -mcmse -MMD -MP -MF"Middlewares/netxduo/Addons DHCP Client/nxd_dhcpv6_client.d" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Middlewares-2f-netxduo-2f-Addons-20-DHCP-20-Client

clean-Middlewares-2f-netxduo-2f-Addons-20-DHCP-20-Client:
	-$(RM) ./Middlewares/netxduo/Addons\ DHCP\ Client/nxd_dhcp_client.cyclo ./Middlewares/netxduo/Addons\ DHCP\ Client/nxd_dhcp_client.d ./Middlewares/netxduo/Addons\ DHCP\ Client/nxd_dhcp_client.o ./Middlewares/netxduo/Addons\ DHCP\ Client/nxd_dhcp_client.su ./Middlewares/netxduo/Addons\ DHCP\ Client/nxd_dhcpv6_client.cyclo ./Middlewares/netxduo/Addons\ DHCP\ Client/nxd_dhcpv6_client.d ./Middlewares/netxduo/Addons\ DHCP\ Client/nxd_dhcpv6_client.o ./Middlewares/netxduo/Addons\ DHCP\ Client/nxd_dhcpv6_client.su

.PHONY: clean-Middlewares-2f-netxduo-2f-Addons-20-DHCP-20-Client

