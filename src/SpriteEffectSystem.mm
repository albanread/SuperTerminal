//
//  SpriteEffectSystem.cpp
//  SuperTerminal Framework
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "../SpriteEffectSystem.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <iostream>
#include <fstream>
#include <sys/stat.h>

namespace SuperTerminal {

// MARK: - EffectParameter Implementation

EffectParameter::EffectParameter(const std::string& n, EffectParameterType t, void* d, size_t size,
                               bool anim, bool config)
    : name(n), type(t), data(d), dataSize(size), animated(anim), userConfigurable(config),
      minValue(0.0f), maxValue(1.0f) {
}

// MARK: - SpriteEffect Implementation

SpriteEffect::SpriteEffect(const std::string& effectName, const std::string& desc)
    : name(effectName), description(desc), pipelineState(nil), renderPass(0),
      requiresMultipass(false), gpuCost(1.0f), requiresDepthBuffer(false),
      requiresStencilBuffer(false), _device(nil) {
}

void SpriteEffect::addParameter(std::shared_ptr<EffectParameter> param) {
    parameters.push_back(param);

    // Create Metal buffer for this parameter if it needs one
    if (param->type != EffectParameterType::TEXTURE) {
        createParameterBuffer(param->name, param->dataSize);
    }
}

std::shared_ptr<EffectParameter> SpriteEffect::getParameter(const std::string& name) {
    for (auto& param : parameters) {
        if (param->name == name) {
            return param;
        }
    }
    return nullptr;
}

void SpriteEffect::setParameterValue(const std::string& name, const void* value, size_t size) {
    auto param = getParameter(name);
    if (!param) {
        NSLog(@"SpriteEffect: Parameter '%s' not found in effect '%s'",
              name.c_str(), this->name.c_str());
        return;
    }

    if (size != param->dataSize) {
        NSLog(@"SpriteEffect: Parameter '%s' size mismatch. Expected %zu, got %zu",
              name.c_str(), param->dataSize, size);
        return;
    }

    // Update the parameter data
    memcpy(param->data, value, size);

    // Update the Metal buffer
    auto bufferIt = parameterBuffers.find(name);
    if (bufferIt != parameterBuffers.end()) {
        id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)bufferIt->second;
        void* bufferContents = [buffer contents];
        memcpy(bufferContents, value, size);
    }
}

void SpriteEffect::createParameterBuffer(const std::string& name, size_t size) {
    if (!_device) {
        NSLog(@"SpriteEffect: Cannot create parameter buffer - device not set");
        return;
    }

    id<MTLDevice> device = (__bridge id<MTLDevice>)_device;
    id<MTLBuffer> buffer = [device newBufferWithLength:size options:MTLResourceStorageModeShared];
    if (!buffer) {
        NSLog(@"SpriteEffect: Failed to create parameter buffer for '%s'", name.c_str());
        return;
    }

    buffer.label = [NSString stringWithFormat:@"SpriteEffect_%s_%s",
                    this->name.c_str(), name.c_str()];
    parameterBuffers[name] = (__bridge void*)buffer;
}

// MARK: - SpriteEffectFactory Implementation

SpriteEffectFactory& SpriteEffectFactory::getInstance() {
    static SpriteEffectFactory instance;
    return instance;
}

void SpriteEffectFactory::registerEffect(const std::string& name, EffectCreator creator) {
    _effectCreators[name] = creator;
    NSLog(@"SpriteEffectFactory: Registered effect '%s'", name.c_str());
}

std::unique_ptr<SpriteEffect> SpriteEffectFactory::createEffect(const std::string& name) {
    auto it = _effectCreators.find(name);
    if (it != _effectCreators.end()) {
        return it->second();
    }
    NSLog(@"SpriteEffectFactory: Unknown effect '%s'", name.c_str());
    return nullptr;
}

std::vector<std::string> SpriteEffectFactory::getAvailableEffects() const {
    std::vector<std::string> effects;
    for (const auto& pair : _effectCreators) {
        effects.push_back(pair.first);
    }
    return effects;
}

std::string SpriteEffectFactory::getEffectDescription(const std::string& name) const {
    // For now, create a temporary effect to get its description
    auto it = _effectCreators.find(name);
    if (it != _effectCreators.end()) {
        auto effect = it->second();
        return effect->description;
    }
    return "Unknown effect";
}

// MARK: - SpriteEffectManager Implementation

SpriteEffectManager::SpriteEffectManager(void* device, void* shaderLibrary)
    : _device(device), _shaderLibrary(shaderLibrary), _performanceMonitoring(false) {

    NSLog(@"SpriteEffectManager: Initializing with device %p", device);

    // Create default pipeline state for fallback rendering
    createDefaultPipelineState();

    // Register built-in effects
    registerBuiltinEffects();

    NSLog(@"SpriteEffectManager: Initialization complete");
}

SpriteEffectManager::~SpriteEffectManager() {
    NSLog(@"SpriteEffectManager: Shutting down");
}

bool SpriteEffectManager::loadEffect(const std::string& effectName) {
    // Check if already loaded
    if (_loadedEffects.find(effectName) != _loadedEffects.end()) {
        NSLog(@"SpriteEffectManager: Effect '%s' already loaded", effectName.c_str());
        return true;
    }

    // Create the effect
    auto effect = SpriteEffectFactory::getInstance().createEffect(effectName);
    if (!effect) {
        NSLog(@"SpriteEffectManager: Failed to create effect '%s'", effectName.c_str());
        return false;
    }

    // Initialize the effect
    if (!effect->initialize(_device, _shaderLibrary)) {
        NSLog(@"SpriteEffectManager: Failed to initialize effect '%s'", effectName.c_str());
        return false;
    }

    // Store the effect
    _loadedEffects[effectName] = std::move(effect);

    NSLog(@"SpriteEffectManager: Successfully loaded effect '%s'", effectName.c_str());
    return true;
}

void SpriteEffectManager::unloadEffect(const std::string& effectName) {
    auto it = _loadedEffects.find(effectName);
    if (it != _loadedEffects.end()) {
        NSLog(@"SpriteEffectManager: Unloading effect '%s'", effectName.c_str());

        // Remove all sprite assignments using this effect
        auto assignmentIt = _spriteEffectAssignments.begin();
        while (assignmentIt != _spriteEffectAssignments.end()) {
            if (assignmentIt->second == effectName) {
                assignmentIt = _spriteEffectAssignments.erase(assignmentIt);
            } else {
                ++assignmentIt;
            }
        }

        // Remove the effect
        _loadedEffects.erase(it);
    }
}

void SpriteEffectManager::reloadEffect(const std::string& effectName) {
    NSLog(@"SpriteEffectManager: Reloading effect '%s'", effectName.c_str());
    unloadEffect(effectName);
    loadEffect(effectName);
}

void SpriteEffectManager::setSpriteEffect(uint16_t spriteId, const std::string& effectName) {
    // Ensure the effect is loaded
    if (!loadEffect(effectName)) {
        NSLog(@"SpriteEffectManager: Cannot assign effect '%s' to sprite %d - failed to load",
              effectName.c_str(), spriteId);
        return;
    }

    _spriteEffectAssignments[spriteId] = effectName;
    NSLog(@"SpriteEffectManager: Assigned effect '%s' to sprite %d", effectName.c_str(), spriteId);
}

void SpriteEffectManager::clearSpriteEffect(uint16_t spriteId) {
    auto it = _spriteEffectAssignments.find(spriteId);
    if (it != _spriteEffectAssignments.end()) {
        NSLog(@"SpriteEffectManager: Cleared effect from sprite %d", spriteId);
        _spriteEffectAssignments.erase(it);
    }
}

void SpriteEffectManager::clearAllSpriteEffects() {
    NSLog(@"SpriteEffectManager: Clearing all sprite effects (%zu assignments)", _spriteEffectAssignments.size());
    _spriteEffectAssignments.clear();
    _perSpriteParameters.clear();
    NSLog(@"SpriteEffectManager: All sprite effects cleared");
}

std::string SpriteEffectManager::getSpriteEffect(uint16_t spriteId) const {
    auto it = _spriteEffectAssignments.find(spriteId);
    if (it != _spriteEffectAssignments.end()) {
        return it->second;
    }
    return "basic"; // Default effect
}

void SpriteEffectManager::setEffectParameter(uint16_t spriteId, const std::string& paramName,
                                           const void* value, size_t size) {
    auto effectName = getSpriteEffect(spriteId);
    auto effectIt = _loadedEffects.find(effectName);
    if (effectIt == _loadedEffects.end()) {
        NSLog(@"SpriteEffectManager: Cannot set parameter - effect '%s' not loaded", effectName.c_str());
        return;
    }

    // Store per-sprite parameter override
    std::string key = std::to_string(spriteId) + ":" + paramName;
    std::vector<uint8_t> data(static_cast<const uint8_t*>(value),
                             static_cast<const uint8_t*>(value) + size);
    _perSpriteParameters[effectName][key] = data;

    NSLog(@"SpriteEffectManager: Set parameter '%s' for sprite %d", paramName.c_str(), spriteId);
}

void SpriteEffectManager::applyPerSpriteParameters(const std::string& effectName, uint16_t spriteId) {
    auto effectIt = _loadedEffects.find(effectName);
    if (effectIt == _loadedEffects.end()) {
        return;
    }

    auto perSpriteIt = _perSpriteParameters.find(effectName);
    if (perSpriteIt == _perSpriteParameters.end()) {
        return;
    }

    SpriteEffect* effect = effectIt->second.get();

    // Apply all per-sprite parameters for this sprite
    for (const auto& paramPair : perSpriteIt->second) {
        const std::string& key = paramPair.first;
        const std::vector<uint8_t>& data = paramPair.second;

        // Check if this parameter belongs to the current sprite
        std::string spritePrefix = std::to_string(spriteId) + ":";
        if (key.find(spritePrefix) == 0) {
            std::string paramName = key.substr(spritePrefix.length());
            effect->setParameterValue(paramName, data.data(), data.size());
        }
    }
}

void SpriteEffectManager::setGlobalEffectParameter(const std::string& effectName,
                                                  const std::string& paramName,
                                                  const void* value, size_t size) {
    auto effectIt = _loadedEffects.find(effectName);
    if (effectIt == _loadedEffects.end()) {
        NSLog(@"SpriteEffectManager: Cannot set global parameter - effect '%s' not loaded",
              effectName.c_str());
        return;
    }

    effectIt->second->setParameterValue(paramName, value, size);
    NSLog(@"SpriteEffectManager: Set global parameter '%s' for effect '%s'",
          paramName.c_str(), effectName.c_str());
}

void SpriteEffectManager::updateEffects(float deltaTime) {
    for (auto& pair : _loadedEffects) {
        pair.second->updateParameters(deltaTime);
    }
}

void SpriteEffectManager::renderSpritesWithEffects(void* encoder,
                                                  const std::vector<uint16_t>& spriteIds,
                                                  const float viewProjection[16]) {
    if (spriteIds.empty()) return;

    // Group sprites by effect
    std::map<std::string, std::vector<uint16_t>> groupedSprites;
    groupSpritesByEffect(spriteIds, groupedSprites);

    // Render each effect group
    for (const auto& group : groupedSprites) {
        renderEffectGroup(encoder, group.first, group.second, viewProjection);
    }
}

float SpriteEffectManager::getEffectGPUTime(const std::string& effectName) const {
    auto it = _effectGPUTimes.find(effectName);
    if (it != _effectGPUTimes.end()) {
        return it->second;
    }
    return 0.0f;
}

void SpriteEffectManager::enableHotReload(const std::string& shaderDirectory) {
    _shaderDirectory = shaderDirectory;
    NSLog(@"SpriteEffectManager: Hot-reload enabled for directory: %s", shaderDirectory.c_str());
}

void SpriteEffectManager::checkForShaderUpdates() {
    if (_shaderDirectory.empty()) return;

    // This would check file modification times and reload changed shaders
    // Implementation would scan the shader directory for .metal files
    // and compare modification times with stored values
    NSLog(@"SpriteEffectManager: Checking for shader updates...");
}

SpriteEffect* SpriteEffectManager::getLoadedEffect(const std::string& effectName) const {
    auto it = _loadedEffects.find(effectName);
    if (it != _loadedEffects.end()) {
        return it->second.get();
    }
    return nullptr;
}

// MARK: - Private Helper Methods

void SpriteEffectManager::createDefaultPipelineState() {
    // Create a basic sprite rendering pipeline as fallback
    NSError* error = nil;
    id<MTLDevice> device = (__bridge id<MTLDevice>)_device;
    id<MTLLibrary> library = (__bridge id<MTLLibrary>)_shaderLibrary;

    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.label = @"Default Sprite Pipeline";

    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"sprite_vertex"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"sprite_fragment"];

    if (!vertexFunction || !fragmentFunction) {
        NSLog(@"SpriteEffectManager: Failed to load default sprite shaders");
        return;
    }

    descriptor.vertexFunction = vertexFunction;
    descriptor.fragmentFunction = fragmentFunction;

    // Vertex descriptor matching SpriteVertexIn structure
    MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    // position [[attribute(0)]]
    vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[0].offset = 0;
    vertexDescriptor.attributes[0].bufferIndex = 0;
    // texCoord [[attribute(1)]]
    vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[1].offset = sizeof(float) * 2;
    vertexDescriptor.attributes[1].bufferIndex = 0;
    // color [[attribute(2)]]
    vertexDescriptor.attributes[2].format = MTLVertexFormatFloat4;
    vertexDescriptor.attributes[2].offset = sizeof(float) * 4;
    vertexDescriptor.attributes[2].bufferIndex = 0;
    // spriteId [[attribute(3)]]
    vertexDescriptor.attributes[3].format = MTLVertexFormatUShort;
    vertexDescriptor.attributes[3].offset = sizeof(float) * 8;
    vertexDescriptor.attributes[3].bufferIndex = 0;
    // flags [[attribute(4)]]
    vertexDescriptor.attributes[4].format = MTLVertexFormatUShort;
    vertexDescriptor.attributes[4].offset = sizeof(float) * 8 + sizeof(uint16_t);
    vertexDescriptor.attributes[4].bufferIndex = 0;

    vertexDescriptor.layouts[0].stride = sizeof(float) * 8 + sizeof(uint16_t) * 2; // Complete SpriteVertexIn
    vertexDescriptor.layouts[0].stepRate = 1;
    vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    descriptor.vertexDescriptor = vertexDescriptor;

    // Configure render target
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    descriptor.colorAttachments[0].blendingEnabled = YES;
    descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    _defaultPipelineState = (__bridge void*)pipeline;

    if (!pipeline) {
        NSLog(@"SpriteEffectManager: Failed to create default pipeline state: %@", error.localizedDescription);
    } else {
        NSLog(@"SpriteEffectManager: Created default pipeline state");
    }
}

void SpriteEffectManager::registerBuiltinEffects() {
    // Register built-in effects using their actual names
    SpriteEffectFactory::getInstance().registerEffect("basic",
        []() -> std::unique_ptr<SpriteEffect> {
            return std::make_unique<Effects::BasicEffect>();
        });
    SpriteEffectFactory::getInstance().registerEffect("glow",
        []() -> std::unique_ptr<SpriteEffect> {
            return std::make_unique<Effects::GlowEffect>();
        });
    SpriteEffectFactory::getInstance().registerEffect("shadow",
        []() -> std::unique_ptr<SpriteEffect> {
            return std::make_unique<Effects::ShadowEffect>();
        });
    SpriteEffectFactory::getInstance().registerEffect("outline",
        []() -> std::unique_ptr<SpriteEffect> {
            return std::make_unique<Effects::OutlineEffect>();
        });
    SpriteEffectFactory::getInstance().registerEffect("sepia",
        []() -> std::unique_ptr<SpriteEffect> {
            return std::make_unique<Effects::SepiaEffect>();
        });

    NSLog(@"SpriteEffectManager: Registered built-in effects");
}

void SpriteEffectManager::groupSpritesByEffect(const std::vector<uint16_t>& spriteIds,
                                              std::map<std::string, std::vector<uint16_t>>& groupedSprites) {
    for (uint16_t spriteId : spriteIds) {
        std::string effectName = getSpriteEffect(spriteId);
        groupedSprites[effectName].push_back(spriteId);
    }
}

void SpriteEffectManager::renderEffectGroup(void* encoder,
                                           const std::string& effectName,
                                           const std::vector<uint16_t>& spriteIds,
                                           const float viewProjection[16]) {
    auto effectIt = _loadedEffects.find(effectName);
    if (effectIt == _loadedEffects.end()) {
        NSLog(@"SpriteEffectManager: Effect '%s' not loaded, using default", effectName.c_str());
        id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;
        id<MTLRenderPipelineState> defaultPipeline = (__bridge id<MTLRenderPipelineState>)_defaultPipelineState;
        [metalEncoder setRenderPipelineState:defaultPipeline];
        return;
    }

    auto& effect = effectIt->second;

    // Set the effect's pipeline state
    id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;
    id<MTLRenderPipelineState> pipeline = (__bridge id<MTLRenderPipelineState>)effect->pipelineState;
    [metalEncoder setRenderPipelineState:pipeline];

    // Render each sprite individually with its own parameters
    for (uint16_t spriteId : spriteIds) {
        // Apply per-sprite parameters
        applyPerSpriteParameters(effectName, spriteId);

        // Bind effect parameters for this sprite
        effect->bindParameters(encoder, spriteId);

        // TODO: Actual sprite rendering would happen here
        // This would involve binding vertex buffers, textures, and drawing for this specific sprite
        NSLog(@"SpriteEffectManager: Rendering sprite %d with effect '%s'", spriteId, effectName.c_str());
    }
}

// MARK: - Built-in Effects Implementation

namespace Effects {

bool BasicEffect::initialize(void* device, void* shaderLibrary) {
    _device = device;

    id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;
    id<MTLLibrary> metalLibrary = (__bridge id<MTLLibrary>)shaderLibrary;

    NSError* error = nil;
    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.label = @"Basic Sprite Effect";

    id<MTLFunction> vertexFunction = [metalLibrary newFunctionWithName:@"sprite_sepia_vertex"];
    id<MTLFunction> fragmentFunction = [metalLibrary newFunctionWithName:@"sprite_fragment"];

    if (!vertexFunction || !fragmentFunction) {
        NSLog(@"BasicEffect: Failed to load shader functions");
        return false;
    }

    descriptor.vertexFunction = vertexFunction;
    descriptor.fragmentFunction = fragmentFunction;

    // Vertex descriptor matching VertexIn format (position + texCoord + color)
    MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    // position [[attribute(0)]]
    vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[0].offset = 0;
    vertexDescriptor.attributes[0].bufferIndex = 0;
    // texCoord [[attribute(1)]]
    vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[1].offset = sizeof(float) * 2;
    vertexDescriptor.attributes[1].bufferIndex = 0;
    // color [[attribute(2)]]
    vertexDescriptor.attributes[2].format = MTLVertexFormatFloat4;
    vertexDescriptor.attributes[2].offset = sizeof(float) * 4;
    vertexDescriptor.attributes[2].bufferIndex = 0;

    vertexDescriptor.layouts[0].stride = sizeof(float) * 8; // VertexIn: position + texCoord + color
    vertexDescriptor.layouts[0].stepRate = 1;
    vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    descriptor.vertexDescriptor = vertexDescriptor;

    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    descriptor.colorAttachments[0].blendingEnabled = YES;
    descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    id<MTLRenderPipelineState> pipeline = [metalDevice newRenderPipelineStateWithDescriptor:descriptor error:&error];
    pipelineState = (__bridge void*)pipeline;

    if (!pipeline) {
        NSLog(@"BasicEffect: Failed to create pipeline state: %@", error.localizedDescription);
        return false;
    }

    NSLog(@"BasicEffect: Initialized successfully");
    return true;
}

void BasicEffect::bindParameters(void* encoder, uint16_t spriteId) {
    // Basic effect has no special parameters
}

bool GlowEffect::initialize(void* device, void* shaderLibrary) {
    _device = device;

    id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;
    id<MTLLibrary> metalLibrary = (__bridge id<MTLLibrary>)shaderLibrary;

    NSError* error = nil;
    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.label = @"Glow Sprite Effect";

    id<MTLFunction> vertexFunction = [metalLibrary newFunctionWithName:@"sprite_sepia_vertex"];
    id<MTLFunction> fragmentFunction = [metalLibrary newFunctionWithName:@"sprite_simple_glow_fragment"];

    if (!vertexFunction || !fragmentFunction) {
        NSLog(@"GlowEffect: Failed to load shader functions");
        return false;
    }

    descriptor.vertexFunction = vertexFunction;
    descriptor.fragmentFunction = fragmentFunction;

    // Vertex descriptor matching VertexIn format (position + texCoord + color)
    MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    // position [[attribute(0)]]
    vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[0].offset = 0;
    vertexDescriptor.attributes[0].bufferIndex = 0;
    // texCoord [[attribute(1)]]
    vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[1].offset = sizeof(float) * 2;
    vertexDescriptor.attributes[1].bufferIndex = 0;
    // color [[attribute(2)]]
    vertexDescriptor.attributes[2].format = MTLVertexFormatFloat4;
    vertexDescriptor.attributes[2].offset = sizeof(float) * 4;
    vertexDescriptor.attributes[2].bufferIndex = 0;

    vertexDescriptor.layouts[0].stride = sizeof(float) * 8; // VertexIn: position + texCoord + color
    vertexDescriptor.layouts[0].stepRate = 1;
    vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    descriptor.vertexDescriptor = vertexDescriptor;

    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    descriptor.colorAttachments[0].blendingEnabled = YES;
    descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    id<MTLRenderPipelineState> pipeline = [metalDevice newRenderPipelineStateWithDescriptor:descriptor error:&error];
    pipelineState = (__bridge void*)pipeline;

    if (!pipeline) {
        NSLog(@"GlowEffect: Failed to create pipeline state: %@", error.localizedDescription);
        return false;
    }

    // Create parameters
    auto colorParam = std::make_shared<EffectParameter>("glow_color", EffectParameterType::COLOR,
                                                       &_glowColor, sizeof(_glowColor), false, true);
    colorParam->minValue = 0.0f;
    colorParam->maxValue = 1.0f;
    addParameter(colorParam);

    auto radiusParam = std::make_shared<EffectParameter>("glow_radius", EffectParameterType::FLOAT,
                                                        &_glowRadius, sizeof(_glowRadius), false, true);
    radiusParam->minValue = 1.0f;
    radiusParam->maxValue = 20.0f;
    addParameter(radiusParam);

    auto intensityParam = std::make_shared<EffectParameter>("glow_intensity", EffectParameterType::FLOAT,
                                                           &_glowIntensity, sizeof(_glowIntensity), false, true);
    intensityParam->minValue = 0.0f;
    intensityParam->maxValue = 2.0f;
    addParameter(intensityParam);

    NSLog(@"GlowEffect: Initialized successfully");
    return true;
}

void GlowEffect::bindParameters(void* encoder, uint16_t spriteId) {
    id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;
    // Bind glow color
    auto colorBuffer = parameterBuffers.find("glow_color");
    if (colorBuffer != parameterBuffers.end()) {
        id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)colorBuffer->second;
        [metalEncoder setFragmentBuffer:buffer offset:0 atIndex:3];
    }

    // Bind glow radius
    auto radiusBuffer = parameterBuffers.find("glow_radius");
    if (radiusBuffer != parameterBuffers.end()) {
        id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)radiusBuffer->second;
        [metalEncoder setFragmentBuffer:buffer offset:0 atIndex:4];
    }
}

void GlowEffect::updateParameters(float deltaTime) {
    // Could animate glow intensity here
    if (_glowIntensity > 1.0f) {
        _glowIntensity = 0.5f + 0.5f * sinf(deltaTime * 2.0f);
        setParameterValue("glow_intensity", &_glowIntensity, sizeof(_glowIntensity));
    }
}

bool ShadowEffect::initialize(void* device, void* shaderLibrary) {
    _device = device;

    id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;
    id<MTLLibrary> metalLibrary = (__bridge id<MTLLibrary>)shaderLibrary;

    NSError* error = nil;
    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.label = @"Shadow Sprite Effect";

    id<MTLFunction> vertexFunction = [metalLibrary newFunctionWithName:@"sprite_sepia_vertex"];
    id<MTLFunction> fragmentFunction = [metalLibrary newFunctionWithName:@"sprite_fragment"];

    if (!vertexFunction || !fragmentFunction) {
        NSLog(@"ShadowEffect: Failed to load shader functions");
        return false;
    }

    descriptor.vertexFunction = vertexFunction;
    descriptor.fragmentFunction = fragmentFunction;

    // Vertex descriptor matching VertexIn format (position + texCoord + color)
    MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    // position [[attribute(0)]]
    vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[0].offset = 0;
    vertexDescriptor.attributes[0].bufferIndex = 0;
    // texCoord [[attribute(1)]]
    vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[1].offset = sizeof(float) * 2;
    vertexDescriptor.attributes[1].bufferIndex = 0;
    // color [[attribute(2)]]
    vertexDescriptor.attributes[2].format = MTLVertexFormatFloat4;
    vertexDescriptor.attributes[2].offset = sizeof(float) * 4;
    vertexDescriptor.attributes[2].bufferIndex = 0;

    vertexDescriptor.layouts[0].stride = sizeof(float) * 8; // VertexIn: position + texCoord + color
    vertexDescriptor.layouts[0].stepRate = 1;
    vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    descriptor.vertexDescriptor = vertexDescriptor;

    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    descriptor.colorAttachments[0].blendingEnabled = YES;
    descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    id<MTLRenderPipelineState> pipeline = [metalDevice newRenderPipelineStateWithDescriptor:descriptor error:&error];
    pipelineState = (__bridge void*)pipeline;

    if (!pipeline) {
        NSLog(@"ShadowEffect: Failed to create pipeline state: %@", error.localizedDescription);
        return false;
    }

    // Create parameters
    auto colorParam = std::make_shared<EffectParameter>("shadow_color", EffectParameterType::COLOR,
                                                       &_shadowColor, sizeof(_shadowColor), false, true);
    colorParam->minValue = 0.0f;
    colorParam->maxValue = 1.0f;
    addParameter(colorParam);

    auto offsetParam = std::make_shared<EffectParameter>("shadow_offset", EffectParameterType::VEC2,
                                                        &_shadowOffset, sizeof(_shadowOffset), false, true);
    offsetParam->minValue = -20.0f;
    offsetParam->maxValue = 20.0f;
    addParameter(offsetParam);

    auto blurParam = std::make_shared<EffectParameter>("shadow_blur", EffectParameterType::FLOAT,
                                                      &_shadowBlur, sizeof(_shadowBlur), false, true);
    blurParam->minValue = 0.0f;
    blurParam->maxValue = 10.0f;
    addParameter(blurParam);

    auto intensityParam = std::make_shared<EffectParameter>("shadow_intensity", EffectParameterType::FLOAT,
                                                           &_shadowIntensity, sizeof(_shadowIntensity), false, true);
    intensityParam->minValue = 0.0f;
    intensityParam->maxValue = 2.0f;
    addParameter(intensityParam);

    NSLog(@"ShadowEffect: Initialized successfully");
    return true;
}

void ShadowEffect::bindParameters(void* encoder, uint16_t spriteId) {
    id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;

    // Bind shadow color
    auto colorBuffer = parameterBuffers.find("shadow_color");
    if (colorBuffer != parameterBuffers.end()) {
        id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)colorBuffer->second;
        [metalEncoder setFragmentBuffer:buffer offset:0 atIndex:3];
    }

    // Bind shadow offset
    auto offsetBuffer = parameterBuffers.find("shadow_offset");
    if (offsetBuffer != parameterBuffers.end()) {
        id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)offsetBuffer->second;
        [metalEncoder setFragmentBuffer:buffer offset:0 atIndex:4];
    }

    // Bind shadow blur
    auto blurBuffer = parameterBuffers.find("shadow_blur");
    if (blurBuffer != parameterBuffers.end()) {
        id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)blurBuffer->second;
        [metalEncoder setFragmentBuffer:buffer offset:0 atIndex:5];
    }
}

void ShadowEffect::updateParameters(float deltaTime) {
    // Could animate shadow parameters here
    // For example, subtle shadow movement:
    // _shadowOffset[0] = 3.0f + sinf(deltaTime * 0.5f);
    // _shadowOffset[1] = 3.0f + cosf(deltaTime * 0.3f);
    // setParameterValue("shadow_offset", &_shadowOffset, sizeof(_shadowOffset));
}

bool OutlineEffect::initialize(void* device, void* shaderLibrary) {
    _device = device;

    id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;
    id<MTLLibrary> metalLibrary = (__bridge id<MTLLibrary>)shaderLibrary;

    NSError* error = nil;
    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.label = @"Outline Sprite Effect";

    id<MTLFunction> vertexFunction = [metalLibrary newFunctionWithName:@"sprite_sepia_vertex"];
    id<MTLFunction> fragmentFunction = [metalLibrary newFunctionWithName:@"sprite_simple_outline_fragment"];

    if (!vertexFunction || !fragmentFunction) {
        NSLog(@"OutlineEffect: Failed to load shader functions");
        return false;
    }

    descriptor.vertexFunction = vertexFunction;
    descriptor.fragmentFunction = fragmentFunction;

    // Vertex descriptor matching VertexIn format (position + texCoord + color)
    MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    // position [[attribute(0)]]
    vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[0].offset = 0;
    vertexDescriptor.attributes[0].bufferIndex = 0;
    // texCoord [[attribute(1)]]
    vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[1].offset = sizeof(float) * 2;
    vertexDescriptor.attributes[1].bufferIndex = 0;
    // color [[attribute(2)]]
    vertexDescriptor.attributes[2].format = MTLVertexFormatFloat4;
    vertexDescriptor.attributes[2].offset = sizeof(float) * 4;
    vertexDescriptor.attributes[2].bufferIndex = 0;

    vertexDescriptor.layouts[0].stride = sizeof(float) * 8; // VertexIn: position + texCoord + color
    vertexDescriptor.layouts[0].stepRate = 1;
    vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    descriptor.vertexDescriptor = vertexDescriptor;

    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    descriptor.colorAttachments[0].blendingEnabled = YES;
    descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    id<MTLRenderPipelineState> pipeline = [metalDevice newRenderPipelineStateWithDescriptor:descriptor error:&error];
    pipelineState = (__bridge void*)pipeline;

    if (!pipeline) {
        NSLog(@"OutlineEffect: Failed to create pipeline state: %@", error.localizedDescription);
        return false;
    }

    // Create parameters
    auto colorParam = std::make_shared<EffectParameter>("outline_color", EffectParameterType::COLOR,
                                                       &_outlineColor, sizeof(_outlineColor), false, true);
    colorParam->minValue = 0.0f;
    colorParam->maxValue = 1.0f;
    addParameter(colorParam);

    auto widthParam = std::make_shared<EffectParameter>("outline_width", EffectParameterType::FLOAT,
                                                       &_outlineWidth, sizeof(_outlineWidth), false, true);
    widthParam->minValue = 1.0f;
    widthParam->maxValue = 10.0f;
    addParameter(widthParam);

    NSLog(@"OutlineEffect: Initialized successfully");
    return true;
}

void OutlineEffect::bindParameters(void* encoder, uint16_t spriteId) {
    id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;

    // Bind outline color
    auto colorBuffer = parameterBuffers.find("outline_color");
    if (colorBuffer != parameterBuffers.end()) {
        id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)colorBuffer->second;
        [metalEncoder setFragmentBuffer:buffer offset:0 atIndex:3];
    }

    // Bind outline width
    auto widthBuffer = parameterBuffers.find("outline_width");
    if (widthBuffer != parameterBuffers.end()) {
        id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)widthBuffer->second;
        [metalEncoder setFragmentBuffer:buffer offset:0 atIndex:4];
    }
}

void OutlineEffect::updateParameters(float deltaTime) {
    // Could animate outline parameters here
    // For example, pulsing outline:
    // _outlineWidth = 2.0f + sinf(deltaTime * 3.0f);
    // setParameterValue("outline_width", &_outlineWidth, sizeof(_outlineWidth));
}

// MARK: - SepiaEffect Implementation

bool SepiaEffect::initialize(void* device, void* shaderLibrary) {
    _device = device;

    id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;
    id<MTLLibrary> metalLibrary = (__bridge id<MTLLibrary>)shaderLibrary;

    NSError* error = nil;
    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.label = @"Sepia Sprite Effect";

    id<MTLFunction> vertexFunction = [metalLibrary newFunctionWithName:@"sprite_sepia_vertex"];
    id<MTLFunction> fragmentFunction = [metalLibrary newFunctionWithName:@"sprite_sepia_fragment"];

    if (!vertexFunction || !fragmentFunction) {
        NSLog(@"SepiaEffect: Failed to load shader functions");
        return false;
    }

    descriptor.vertexFunction = vertexFunction;
    descriptor.fragmentFunction = fragmentFunction;

    // Simple vertex descriptor for basic sprite rendering (position, texCoord, color)
    // Vertex descriptor matching VertexIn format (position + texCoord + color)
    MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];
    // position [[attribute(0)]]
    vertexDescriptor.attributes[0].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[0].offset = 0;
    vertexDescriptor.attributes[0].bufferIndex = 0;
    // texCoord [[attribute(1)]]
    vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
    vertexDescriptor.attributes[1].offset = sizeof(float) * 2;
    vertexDescriptor.attributes[1].bufferIndex = 0;
    // color [[attribute(2)]]
    vertexDescriptor.attributes[2].format = MTLVertexFormatFloat4;
    vertexDescriptor.attributes[2].offset = sizeof(float) * 4;
    vertexDescriptor.attributes[2].bufferIndex = 0;

    vertexDescriptor.layouts[0].stride = sizeof(float) * 8; // VertexIn: position + texCoord + color
    vertexDescriptor.layouts[0].stepRate = 1;
    vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    descriptor.vertexDescriptor = vertexDescriptor;

    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    descriptor.colorAttachments[0].blendingEnabled = YES;
    descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    id<MTLRenderPipelineState> pipeline = [metalDevice newRenderPipelineStateWithDescriptor:descriptor error:&error];
    pipelineState = (__bridge void*)pipeline;

    if (!pipeline) {
        NSLog(@"SepiaEffect: Failed to create pipeline state: %@", error.localizedDescription);
        return false;
    }

    // Create parameters
    auto intensityParam = std::make_shared<EffectParameter>("sepia_intensity", EffectParameterType::FLOAT,
                                                           &_sepiaIntensity, sizeof(_sepiaIntensity), false, true);
    intensityParam->minValue = 0.0f;
    intensityParam->maxValue = 1.0f;
    addParameter(intensityParam);

    NSLog(@"SepiaEffect: Initialized successfully");
    return true;
}

void SepiaEffect::bindParameters(void* encoder, uint16_t spriteId) {
    id<MTLRenderCommandEncoder> metalEncoder = (__bridge id<MTLRenderCommandEncoder>)encoder;

    // Bind sepia intensity
    auto intensityBuffer = parameterBuffers.find("sepia_intensity");
    if (intensityBuffer != parameterBuffers.end()) {
        id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)intensityBuffer->second;
        [metalEncoder setFragmentBuffer:buffer offset:0 atIndex:3];
    }
}

void SepiaEffect::updateParameters(float deltaTime) {
    // Could animate sepia intensity here if needed
    // For example, fading in/out sepia effect:
    // _sepiaIntensity = 0.5f + 0.5f * sinf(deltaTime * 2.0f);
    // setParameterValue("sepia_intensity", &_sepiaIntensity, sizeof(_sepiaIntensity));
}

} // namespace Effects

} // namespace SuperTerminal

// MARK: - C Interface Implementation

using namespace SuperTerminal;

static SpriteEffectManager* g_effectManager = nullptr;

extern "C" {

void sprite_effect_init(void* device, void* shaderLibrary) {
    if (!g_effectManager) {
        g_effectManager = new SpriteEffectManager((__bridge id<MTLDevice>)device,
                                                 (__bridge id<MTLLibrary>)shaderLibrary);
    }
}

void sprite_effect_shutdown() {
    if (g_effectManager) {
        delete g_effectManager;
        g_effectManager = nullptr;
    }
}

void sprite_effect_load(const char* effectName) {
    if (g_effectManager) {
        g_effectManager->loadEffect(effectName);
    }
}

void sprite_effect_unload(const char* effectName) {
    if (g_effectManager) {
        g_effectManager->unloadEffect(effectName);
    }
}

void sprite_set_effect(uint16_t spriteId, const char* effectName) {
    if (g_effectManager) {
        g_effectManager->setSpriteEffect(spriteId, effectName);
    }
}

void sprite_clear_effect(uint16_t spriteId) {
    if (g_effectManager) {
        g_effectManager->clearSpriteEffect(spriteId);
    }
}

void sprite_clear_all_effects() {
    if (g_effectManager) {
        g_effectManager->clearAllSpriteEffects();
    }
}

const char* sprite_get_effect(uint16_t spriteId) {
    if (g_effectManager) {
        static std::string result;
        result = g_effectManager->getSpriteEffect(spriteId);
        return result.c_str();
    }
    return "basic";
}

void sprite_effect_set_float(uint16_t spriteId, const char* paramName, float value) {
    if (g_effectManager) {
        g_effectManager->setEffectParameter(spriteId, paramName, &value, sizeof(value));
    }
}

void sprite_effect_set_vec2(uint16_t spriteId, const char* paramName, float x, float y) {
    if (g_effectManager) {
        float vec2[2] = {x, y};
        g_effectManager->setEffectParameter(spriteId, paramName, &vec2, sizeof(vec2));
    }
}

void sprite_effect_set_color(uint16_t spriteId, const char* paramName, float r, float g, float b, float a) {
    if (g_effectManager) {
        float color[4] = {r, g, b, a};
        g_effectManager->setEffectParameter(spriteId, paramName, &color, sizeof(color));
    }
}

void sprite_effect_set_global_float(const char* effectName, const char* paramName, float value) {
    if (g_effectManager) {
        g_effectManager->setGlobalEffectParameter(effectName, paramName, &value, sizeof(value));
    }
}

void sprite_effect_set_global_vec2(const char* effectName, const char* paramName, float x, float y) {
    if (g_effectManager) {
        float vec2[2] = {x, y};
        g_effectManager->setGlobalEffectParameter(effectName, paramName, &vec2, sizeof(vec2));
    }
}

void sprite_effect_set_global_color(const char* effectName, const char* paramName,
                                   float r, float g, float b, float a) {
    if (g_effectManager) {
        float color[4] = {r, g, b, a};
        g_effectManager->setGlobalEffectParameter(effectName, paramName, &color, sizeof(color));
    }
}

const char** sprite_effect_get_available_effects(int* count) {
    if (g_effectManager && count) {
        auto effects = SpriteEffectFactory::getInstance().getAvailableEffects();
        static std::vector<const char*> effectNames;
        effectNames.clear();
        for (const auto& name : effects) {
            effectNames.push_back(name.c_str());
        }
        *count = static_cast<int>(effectNames.size());
        return effectNames.data();
    }
    if (count) *count = 0;
    return nullptr;
}

float sprite_effect_get_gpu_time(const char* effectName) {
    if (g_effectManager) {
        return g_effectManager->getEffectGPUTime(effectName);
    }
    return 0.0f;
}

void sprite_effect_enable_monitoring(bool enable) {
    if (g_effectManager) {
        g_effectManager->enablePerformanceMonitoring(enable);
    }
}

void sprite_effect_enable_hot_reload(const char* shaderDirectory) {
    if (g_effectManager) {
        g_effectManager->enableHotReload(shaderDirectory);
    }
}

void sprite_effect_check_updates() {
    if (g_effectManager) {
        g_effectManager->checkForShaderUpdates();
    }
}

void* sprite_effect_get_pipeline_state(uint16_t spriteId) {
    if (!g_effectManager) {
        return nullptr;
    }

    std::string effectName = g_effectManager->getSpriteEffect(spriteId);
    if (effectName == "basic") {
        return nullptr;  // Use default sprite pipeline
    }

    SpriteEffect* effect = g_effectManager->getLoadedEffect(effectName);
    if (effect) {
        return effect->pipelineState;
    }

    return nullptr;
}

void sprite_effect_bind_parameters(uint16_t spriteId, void* encoder) {
    if (!g_effectManager) {
        return;
    }

    std::string effectName = g_effectManager->getSpriteEffect(spriteId);
    if (effectName == "basic") {
        return;  // No parameters to bind for basic rendering
    }

    // Apply per-sprite parameter overrides before binding
    g_effectManager->applyPerSpriteParameters(effectName, spriteId);

    SpriteEffect* effect = g_effectManager->getLoadedEffect(effectName);
    if (effect) {
        effect->bindParameters(encoder, spriteId);
    }
}

} // extern "C"
