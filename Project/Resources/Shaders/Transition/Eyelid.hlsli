// 瞼（まぶた）で視界が閉じる形。羽根の着水（死亡）で閉じ、来週のスタート演出で逆再生（開く）に使う。
// 上下の瞼の縁は、目の中央が一番開いていて端ほど閉じる弧（アーモンド形）。閉じる点は画面の中心より少し下
// （上の瞼の方が大きく下りる）。縁はぼかし、縁の近くはまつ毛の影で少し暗くする。

#ifndef EYELID_HLSLI
#define EYELID_HLSLI

struct EyelidParams
{
    float closure;     // 0 = 開いている（瞼は画面の外）/ 1 = 閉じ切った
    float curvature;   // 縁の弧の強さ（0 = まっすぐ / 0.6 前後でアーモンド形）
    float softness;    // 縁のぼかし幅（画面の高さに対する割合）
    float closeY;      // 閉じる点の高さ（画面 UV。0.5 が中心、大きいほど下）
};

// 見えている度合い 0..1（1 = 瞼の内側）
float EyelidVisible(EyelidParams lid, float2 uv)
{
    const float closure = saturate(lid.closure);
    const float x = uv.x * 2.0f - 1.0f;                    // -1..1
    const float shape = saturate(1.0f - lid.curvature * x * x); // 中央 1、端ほど小さい

    // 開き切ったときに画面の角まで瞼が来ないだけの開き（縁のぼかしの分も足す）
    const float fullOpen = (0.5f + 2.0f * lid.softness) / max(1.0f - lid.curvature, 0.05f) + abs(lid.closeY - 0.5f);
    const float s = max(lid.softness, 1e-4f);
    // 閉じ切ったときは縁のぼかし同士が重ならないよう、少し食い込ませる（中央に光が残らない）
    const float open = fullOpen * (1.0f - closure) - 2.0f * s * closure;
    const float centerY = lerp(0.5f, lid.closeY, closure);

    const float upper = centerY - open * shape;
    const float lower = centerY + open * shape;
    const float inside = smoothstep(upper - s, upper + s, uv.y) * smoothstep(lower + s, lower - s, uv.y);

    // まつ毛の影：縁から少し内側まで暗く
    const float edgeDistance = min(uv.y - upper, lower - uv.y);
    const float lash = lerp(0.55f, 1.0f, smoothstep(0.0f, s * 6.0f, edgeDistance));
    return inside * lash;
}

#endif // EYELID_HLSLI
