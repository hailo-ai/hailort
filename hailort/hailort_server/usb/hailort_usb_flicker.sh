#!/usr/bin/env bash

# On Mercury LED is controlled by GPIO21: LED is ON when GPIO21 is set to LOW.
# /dev/gpiochip0 -> lanes 0-15, /dev/gpiochip1 -> lanes 16-31
# Therefore GPIO21 == gpiochip1[5]
gpio_io_output_en_n_ctrl_bypass_val=0x7C292038
gpio_io_output_bypass_val=0x7C292034
gpio_io_output_en_n_ctrl_bypass_en=0x7C292028
gpio_io_output_bypass_en=0x7C292024

# On Mars LED is controlled by GPIO21
# LED is ON when GPIO21 is set to LOW.
led_gpio_bit=21

# TODO HRT-20195: GPIO should be controlled using `gpioset` util.
set_bit () {
    local reg=$1
    local bit=$2
    local curr=`devmem $reg 32`
    local new=$(( curr | (1 << bit) ))
    devmem $reg 32 $new
}

unset_bit () {
    local reg=$1
    local bit=$2
    local curr=`devmem $reg 32`
    local new=$(( curr & ~(1 << bit) ))
    devmem $reg 32 $new
}

led_off () {
    unset_bit $gpio_io_output_en_n_ctrl_bypass_val $led_gpio_bit
    set_bit   $gpio_io_output_bypass_val           $led_gpio_bit
    set_bit   $gpio_io_output_en_n_ctrl_bypass_en  $led_gpio_bit
    set_bit   $gpio_io_output_bypass_en            $led_gpio_bit
}

led_on () {
    set_bit   $gpio_io_output_en_n_ctrl_bypass_val $led_gpio_bit
    set_bit   $gpio_io_output_bypass_val           $led_gpio_bit
    unset_bit $gpio_io_output_en_n_ctrl_bypass_en  $led_gpio_bit
    unset_bit $gpio_io_output_bypass_en            $led_gpio_bit
}

led_flicker () {
    led_off
    sleep 0.2
    led_on
}

get_nnc_in_use () {
    vdma_mon_path=/sys/class/hailo1x_integrated/h1x/vdma_monitor
    echo `cat $vdma_mon_path | tail -n1 | cut -f1`
}

main () {
    local nnc_in_use=`get_nnc_in_use`
    led_on
    while true; do
        local curr_nnc_in_use=`get_nnc_in_use`
        if [ $curr_nnc_in_use -gt $nnc_in_use ]; then
            nnc_in_use=$curr_nnc_in_use
            led_flicker
        fi
        sleep 0.5
    done
}

main
