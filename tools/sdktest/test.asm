; Настоящий sdk/trinity_gs.asm для теста: таблица входов на постоянных адресах
        device zxspectrum128
        org #8000
        jp Gsc_Detect           ; +0
        jp Gsc_Close            ; +3
        jp Gsc_Cfg_Get          ; +6
        jp Gsc_Cfg_Set          ; +9
        jp Gsc_Save             ; +12
        jp Gsc_Preset           ; +15
        jp Gsc_Stat             ; +18
        jp Gsc_Stat_Reset       ; +21
        jp Gsc_Curve            ; +24
        jp Gsc_Version          ; +27
        include "sdk/trinity_gs.asm"
        savebin "sdktest.bin",#8000,$-#8000
