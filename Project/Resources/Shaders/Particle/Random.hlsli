// GPU側で乱数を生成するためのユーティリティ
// rand3dTo3dは値を入れると擬似乱数を返す関数。同じseedからは同じ値が出る

float rand3dTo1d(float3 value, float3 dotDir)
{
    float3 smallValue = sin(value);
    float random = dot(smallValue, dotDir);
    random = frac(sin(random) * 143758.5453f);
    return random;
}

float3 rand3dTo3d(float3 value)
{
    return float3(
        rand3dTo1d(value, float3(12.989f, 78.233f, 37.719f)),
        rand3dTo1d(value, float3(39.346f, 11.135f, 83.155f)),
        rand3dTo1d(value, float3(73.156f, 52.235f, 09.151f))
    );
}

// 1つのCS内で複数回別々の乱数を取りたいケース向けのクラス
// 自分自身に代入し続けることで、呼び出すたびに別の値が出る
class RandomGenerator
{
    float3 seed;

    float3 Generate3d()
    {
        seed = rand3dTo3d(seed);
        return seed;
    }

    float Generate1d()
    {
        float result = rand3dTo1d(seed, float3(12.989f, 78.233f, 37.719f));
        seed.x = result;
        return result;
    }
};
