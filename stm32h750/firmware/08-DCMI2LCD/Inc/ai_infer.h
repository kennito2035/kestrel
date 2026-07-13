/*
 * ai_infer.h - thin wrapper around the X-CUBE-AI generated network.
 * Lives outside CubeMX's generated set so code regeneration never touches it.
 */
#ifndef AI_INFER_H
#define AI_INFER_H

#include <stdint.h>

/* Initializes CRC clock, DWT cycle counter and the network instance.
 * Returns 0 on success, negative ai_error code otherwise. */
int ai_infer_init(void);

/* Runs one inference on whatever is in the input buffer.
 * Returns wall time in milliseconds (DWT cycle-exact), or 0 on failure. */
uint32_t ai_infer_run(void);

/* Runs `iters` back-to-back inferences (min 2). First run is treated as the
 * cold-cache run and reported via *cold_ms; the rest are warm. Returns the
 * warm-cache minimum (best steady-state) in ms; *warm_max_ms gets the warm max.
 * Pointers may be NULL. */
uint32_t ai_infer_bench(uint32_t iters, uint32_t *cold_ms, uint32_t *warm_max_ms);

/* Fills the input tensor with a flat gray test pattern (smoke test). */
void ai_infer_fill_dummy_input(void);

/* Direct access to the network IO buffers (input: uint8 RGB 192x192x3,
 * output: float 12x12x30 YOLO grid). Size out-params may be NULL. */
uint8_t *ai_infer_input_buffer(uint32_t *size_bytes);
float   *ai_infer_output_buffer(uint32_t *size_bytes);

#endif /* AI_INFER_H */
