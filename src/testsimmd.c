#include <immintrin.h>

void update_positions_avx(float* x, float* y, float* dx, float* dy, int count, float delta) {
    __m256 delta_v = _mm256_set1_ps(delta);
    for (int i = 0; i < count; i += 8) {
        __m256 vx = _mm256_loadu_ps(x + i);
        __m256 vy = _mm256_loadu_ps(y + i);
        __m256 vdx = _mm256_loadu_ps(dx + i);
        __m256 vdy = _mm256_loadu_ps(dy + i);

        vx = _mm256_fmadd_ps(vdx, delta_v, vx);  // vx += vdx * delta
        vy = _mm256_fmadd_ps(vdy, delta_v, vy);

        _mm256_storeu_ps(x + i, vx);
        _mm256_storeu_ps(y + i, vy);
    }
}
void main(void) {
    float x[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    float y[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    float dx[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    float dy[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    update_positions_avx(x, y, dx, dy, 8, .01);
}