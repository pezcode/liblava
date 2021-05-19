struct UboData
{
    vec3 camPos;
    float _padding;
    mat4 view;
    mat4 projection;
    mat4 invProjection;
    uvec2 resolution;
    uint lightCount;
};

struct PushConstantData
{
    mat4 model;
    vec3 color;
    float metallic;
};

struct LightData
{
    vec3 position;
    float radius;
    vec3 intensity;
    float _padding;
};
