#include "Volumetric.h"

#include <frm/core/def.h>
#include <frm/core/Buffer.h>
#include <frm/core/Framebuffer.h>
#include <frm/core/GlContext.h>
#include <frm/core/Profiler.h>
#include <frm/core/Shader.h>
#include <frm/core/Texture.h>

#include <apt/ArgList.h>

using namespace frm;
using namespace apt;

static Volumetric s_inst;

Volumetric::Volumetric()
	: AppBase("Volumetric") 
{
	PropertyGroup& propGroup = m_props.addGroup("Volumetric");
	//                  name                                     default                               min            max        storage
	propGroup.addFloat ("m_volumeData.m_volumeWidth",            m_volumeData.m_volumeWidth,           0.0f,          FLT_MAX,   &m_volumeData.m_volumeWidth);
	propGroup.addFloat ("m_volumeData.m_volumeHeight",           m_volumeData.m_volumeHeight,          0.0f,          FLT_MAX,   &m_volumeData.m_volumeHeight);
	propGroup.addFloat ("m_volumeData.m_coverageBias",           m_volumeData.m_coverageBias,         -1.0f,          1.0f,      &m_volumeData.m_coverageBias);
	propGroup.addFloat ("m_volumeData.m_density",                m_volumeData.m_density,               0.0f,          10.0f,     &m_volumeData.m_density);
	propGroup.addFloat ("m_volumeData.m_scatter",                m_volumeData.m_scatter,               0.0f,          1.0f,      &m_volumeData.m_scatter);
	propGroup.addFloat ("m_volumeData.m_shapeScale",             m_volumeData.m_shapeScale,            0.0f,          0.5f,      &m_volumeData.m_shapeScale);
	propGroup.addFloat ("m_volumeData.m_erosionScale",           m_volumeData.m_erosionScale,          0.0f,          20.0f,     &m_volumeData.m_erosionScale);
	propGroup.addFloat ("m_volumeData.m_erosionStrength",        m_volumeData.m_erosionStrength,       0.0f,          1.0f,      &m_volumeData.m_erosionStrength);
	propGroup.addFloat ("m_sunPath.m_theta",                     m_sunPath.m_theta,                    0.0f,          1.0f,      &m_sunPath.m_theta);
	propGroup.addFloat ("m_sunPath.m_azimuth",                   m_sunPath.m_azimuth,                  0.0f,          6.3f,      &m_sunPath.m_azimuth);
	propGroup.addFloat ("m_sunPath.m_elevation",                 m_sunPath.m_elevation,                0.0f,          3.2f,      &m_sunPath.m_elevation);
}

Volumetric::~Volumetric()
{
}

bool Volumetric::init(const apt::ArgList& _args)
{
	if (!AppBase::init(_args)) 
	{
		return false;
	}
	
	m_txNoiseShape = Texture::Create("textures/noise_shape.tga",   Texture::SourceLayout_VolumeNx1);
	m_txNoiseShape->generateMipmap();
	m_txNoiseShape->setName("txNoiseShape");
	m_txNoiseErosion = Texture::Create("textures/noise_erosion.tga", Texture::SourceLayout_VolumeNx1);
	m_txNoiseErosion->generateMipmap();
	m_txNoiseErosion->setName("txNoiseErosion");
	m_txCloudControl = Texture::Create("textures/cloud_control.tga");
	m_txCloudControl->setName("txCloudControl");
	m_txCloudPhase = Texture::Create("textures/cloud_phase.psd");
	m_txCloudPhase->setName("txCloudPhase");
	m_txCloudPhase->setWrap(GL_CLAMP_TO_EDGE);

	m_txSceneColor = Texture::Create2d(m_resolution.x, m_resolution.y, GL_RGBA16F);
	m_txSceneColor->setName("txSceneColor");
	m_txSceneDepth = Texture::Create2d(m_resolution.x, m_resolution.y, GL_DEPTH_COMPONENT32F);
	m_txSceneDepth->setName("txSceneDepth");
	m_fbScene = Framebuffer::Create(2, m_txSceneColor, m_txSceneDepth);

	m_shTest = Shader::CreateCs("shaders/test_cs.glsl", 8, 8);

	return true;
}

void Volumetric::shutdown()
{
	Shader::Release(m_shTest);

	Buffer::Destroy(m_bfVolumeData);
	Texture::Release(m_txCloudControl);
	Texture::Release(m_txNoiseShape);
	Texture::Release(m_txNoiseErosion);
	Texture::Release(m_txSceneColor);
	Texture::Release(m_txSceneDepth);
	Framebuffer::Destroy(m_fbScene);

	AppBase::shutdown();
}

bool Volumetric::update()
{
	if (!AppBase::update()) 
	{
		return false;
	}

	bool requireUpdate = false;
	APT_ONCE requireUpdate = true;

	ImGui::SetNextTreeNodeOpen(true, ImGuiCond_Once);
	if (ImGui::TreeNode("Volume"))
	{
		requireUpdate |= editVolumeData();

		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Sun Path"))
	{
		m_sunPath.m_radius = 8000.0f;
		requireUpdate |= m_sunPath.edit();

		ImGui::TreePop();
	}
	m_sunPath.apply((float)getDeltaTime());

	if (requireUpdate)
	{
		updateVolumeDataGPU();
	}

	return true;
}

void Volumetric::draw()
{
	GlContext* ctx = GlContext::GetCurrent();

	Camera* drawCamera = Scene::GetDrawCamera();
	Camera* cullCamera = Scene::GetCullCamera();

	{	PROFILER_MARKER_GPU("Test");

		ctx->setShader(m_shTest);
		ctx->bindBuffer(drawCamera->m_gpuBuffer);
		ctx->bindBuffer(m_bfVolumeData);
		ctx->bindTexture(m_txNoiseShape);
		ctx->bindTexture(m_txNoiseErosion);
		ctx->bindTexture(m_txCloudControl);
		ctx->bindTexture(m_txCloudPhase);
		ctx->bindImage("txDst", m_txSceneColor, GL_WRITE_ONLY);
		ctx->dispatch(m_txSceneColor);
	}

	ctx->blitFramebuffer(m_fbScene, nullptr);

	AppBase::draw();
}

// PRIVATE

bool Volumetric::editVolumeData()
{
	bool ret = false;

	ret |= ImGui::SliderFloat("Volume Width", &m_volumeData.m_volumeWidth, 10.0f, 10000.0f);
	ret |= ImGui::SliderFloat("Volume Height", &m_volumeData.m_volumeHeight, 1.0f, 10000.0f);
	ImGui::Spacing();
	ret |= ImGui::SliderFloat("Density", &m_volumeData.m_density, 0.0f, 4.0f);
	ret |= ImGui::SliderFloat("Scatter", &m_volumeData.m_scatter, 0.0f, 1.0f);
	ImGui::Spacing();
	ret |= ImGui::SliderFloat("Coverage Bias", &m_volumeData.m_coverageBias, -1.0f, 1.0f);
	ret |= ImGui::SliderFloat("Shape Scale", &m_volumeData.m_shapeScale, 0.0f, 0.25f);
	ret |= ImGui::SliderFloat("Erosion Scale", &m_volumeData.m_erosionScale, 0.0f, 10.0f);
	ret |= ImGui::SliderFloat("Erosion Strength", &m_volumeData.m_erosionStrength, 0.0f, 1.0f);

	float boxHalfWidth  = m_volumeData.m_volumeWidth  / 2.0f;
	float boxHalfHeight = m_volumeData.m_volumeHeight / 2.0f;
	vec3  boxHalfExtent = vec3(boxHalfWidth, boxHalfHeight, boxHalfWidth);

	if (m_showHelpers)
	{
		ret |= Im3d::GizmoTranslation("Volume Origin", &m_volumeData.m_volumeOrigin.x);
		Im3d::PushDrawState();
			Im3d::SetColor(Im3d::Color_Magenta);
			Im3d::SetSize(3.0f);
			Im3d::DrawAlignedBox(m_volumeData.m_volumeOrigin - boxHalfExtent, m_volumeData.m_volumeOrigin + boxHalfExtent);
		Im3d::PopDrawState();
	}

	return ret;
}

void Volumetric::updateVolumeDataGPU()
{
	struct VolumeDataGPU
	{
		vec4  m_volumeExtentMin;
		vec4  m_volumeExtentMax;
		vec4  m_lightDirection;
		
		float m_coverageBias;
		float m_density;
		float m_scatter;
		float m_shapeScale;
		float m_erosionScale;
		float m_erosionStrength;
	};
	VolumeDataGPU volumeDataGPU;
	
	float boxHalfWidth  = m_volumeData.m_volumeWidth  / 2.0f;
	float boxHalfHeight = m_volumeData.m_volumeHeight / 2.0f;
	vec3  boxHalfExtent = vec3(boxHalfWidth, boxHalfHeight, boxHalfWidth);

	volumeDataGPU.m_volumeExtentMin.xyz() = m_volumeData.m_volumeOrigin - boxHalfExtent;
	volumeDataGPU.m_volumeExtentMax.xyz() = m_volumeData.m_volumeOrigin + boxHalfExtent;
	volumeDataGPU.m_lightDirection.xyz()  = m_sunPath.m_direction;
	volumeDataGPU.m_coverageBias          = m_volumeData.m_coverageBias;
	volumeDataGPU.m_density               = m_volumeData.m_density;
	volumeDataGPU.m_scatter               = m_volumeData.m_scatter;
	volumeDataGPU.m_shapeScale            = m_volumeData.m_shapeScale;
	volumeDataGPU.m_erosionScale          = m_volumeData.m_shapeScale * m_volumeData.m_erosionScale;
	volumeDataGPU.m_erosionStrength       = m_volumeData.m_erosionStrength;

	if (!m_bfVolumeData)
	{
		m_bfVolumeData = Buffer::Create(GL_SHADER_STORAGE_BUFFER, sizeof(volumeDataGPU), GL_DYNAMIC_STORAGE_BIT);
		m_bfVolumeData->setName("bfVolumeData");
	}
	m_bfVolumeData->setData(sizeof(volumeDataGPU), &volumeDataGPU);
}