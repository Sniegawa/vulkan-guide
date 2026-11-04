#pragma once
#include "SDL_events.h"
#include <vk_types.h>

class Camera {
public:
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 5.0f);

    float pitch = 0.0f;
    float yaw = 0.0f;


    glm::mat4 getViewMatrix();
    glm::mat4 getRotationMatrix();

    void processSDLEvent(SDL_Event& e);

    void update();
};
