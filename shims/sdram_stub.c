#ifdef __cplusplus
extern "C" {
#endif

// DaisyDuino calls this during AudioClass::ConfigureSdram().
// We run without SDRAM under Arduino's HAL, so make it a no-op.
int dsy_sdram_init(void) { return 0; }

#ifdef __cplusplus
}
#endif
