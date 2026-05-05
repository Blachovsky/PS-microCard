#include "ps1_card.h"
#include "microSD.h"
#include "pico/stdlib.h"
#include "hardware_config.h"

int main(void){
    stdio_init_all();
    sleep_ms(500);
    ps1_mc_gpio_init();
    printf("\nPS1 memory card full restore test\n");
    bool test_status = test_backup_ps1_card_to_microsd(BACKUP_PATH);
    //bool test_status = test_restore_microsd_to_ps1_card(RESTORE_PATH);
    if(test_status){
        printf("Test success");
    }else{
        printf("Test failed");
    }
    while(true){sleep_ms(1000);}
    return 0;
}
