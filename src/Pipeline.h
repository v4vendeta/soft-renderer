#pragma once
#include <vector>
#include <array>
#include <cstdint>
#include <omp.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Model.h"
#include "ShaderStage.h"
#include "Light.h"
#include "Camera.h"
#include <omp.h>

class Pipeline
{
public:
	Pipeline(uint32_t w, uint32_t h) : width(w), height(h)
	{
		color_buffer.resize(3 * width * height);
		std::fill(color_buffer.begin(), color_buffer.end(), 0);
		depth_buffer.resize(width * height);
		std::fill(depth_buffer.begin(), depth_buffer.end(), 1.0f);


	}
	~Pipeline()
	{
	}

	void InitAsset() {

		projection = glm::perspective(glm::radians(90.0f), static_cast<float>(width) / height, 0.1f, 100.0f);
		glm::mat4 id(1.0f);

		Model plane("../resources/model/plane/plane.obj");
		plane.BindTexture("../resources/model/plane/container.jpg");
		glm::mat4 plane_t{ glm::translate(id, glm::vec3(1.0f, -2.0f, 0.0f)) };
		plane.SetModelMat(plane_t);


		Model backpack("../resources/model/backpack/backpack.obj");
		backpack.BindTexture("../resources/model/backpack/diffuse.jpg");
		glm::mat4 backpack_t{ glm::translate(id, glm::vec3(4.0f, 0.0f, 0.0f)) };
		//backpack_t = { glm::scale(backpack_t,glm::vec3(0.5,0.5,0.5)) };
		backpack.SetModelMat(backpack_t);

		Model crate("../resources/model/Crate/crate_tri.obj");
		crate.BindTexture("../resources/model/Crate/crate_1.jpg");
		crate.SetModelMat(glm::translate(id, glm::vec3(0.0f, 2.0f, 0.0f)));

		Model sphere("../resources/model/sphere/sphere.obj");
		sphere.BindTexture("../resources/model/sphere/rustediron2_basecolor.png");



		models.push_back(plane);
		models.push_back(crate);
		models.push_back(backpack);
		models.push_back(sphere);
		for (auto model : models) { face_count += model.GetTriangleCount(); }
	}


	void Update(std::shared_ptr<Camera> cam, float delta_time) {
		//std::cout <<face_count<<", "<< face_culled << "\n";
		face_culled = 0;
		view = cam->GetViewMatrix();
		// light rotate by y
		glm::mat4 rot{ glm::rotate(glm::mat4(1.0f), glm::radians(0.02f * delta_time), glm::vec3(0.0, 1.0, 0.0)) };
		light.Transform(rot);
		//plight.SetRot(rot);

		//glm::mat4 rot_plane{ glm::rotate(models[0].GetModelMat(), glm::radians(1000.0f * 0.2f * delta_time), glm::vec3(0.0, 1.0, 0.0)) };

		//models[0].SetModelMat(rot_plane);

		//glm::mat4 rot_crate{ glm::rotate(models[1].GetModelMat(), glm::radians( 1000.0f*0.1f* delta_time), glm::vec3(1.0, 0.0, 0.0)) };
		//models[1].SetModelMat(rot_crate);
		//models[2].SetModelMat(rot_crate);
	}

	void Render()
	{
		vs_input vsin{};
		vsin.view = view;
		ps_input psin{};
		psin.light_color = light.Color();
		//psin.view_light_pos = view * plight.Pos();
		psin.view_light_dir = view * light.Dir();

		for (auto& single_model : models) {

			vsin.model = single_model.GetModelMat();
			//std::cout << vsin.model[0][0]<<"\n";
			glm::mat4 mvp = projection * view * single_model.GetModelMat();
			vsin.mvp = mvp;
			psin.texture = single_model.GetTexture();
			auto tri_list = single_model.GetTriangleList();
			for (int32_t y = 0; y < tri_list.size(); y++) {
				
				vsin.vertex = { tri_list[y].GetPos(0), tri_list[y].GetPos(1),tri_list[y].GetPos(2) };
				vsin.normal = { tri_list[y].GetNormal(0),tri_list[y].GetNormal(1) ,tri_list[y].GetNormal(2) };
				vsin.uv = { tri_list[y].GetTexcoord(0),tri_list[y].GetTexcoord(1),tri_list[y].GetTexcoord(2) };

				// model space to clip space
				vs_output vso{ VertexShader(vsin) };
				// clipping
				if (!Clipping(vso))continue;
				// clip to ndc
				PerspectiveDivision(vso);
				if (!CullBackFace(vso))continue;
				// viewport transformation
				ScreenMapping(vso);
				PerspectiveCorrection(vso);


				// rasterize and pixel processing

				int32_t max_x = std::max({ vso.pos[0].x, vso.pos[1].x, vso.pos[2].x });
				int32_t max_y = std::max({ vso.pos[0].y, vso.pos[1].y, vso.pos[2].y });
				int32_t min_x = std::min({ vso.pos[0].x, vso.pos[1].x, vso.pos[2].x });
				int32_t min_y = std::min({ vso.pos[0].y, vso.pos[1].y, vso.pos[2].y });


					for (int32_t i = min_x; i <= max_x; ++i) {
						for (int32_t j = min_y; j <= max_y; ++j) {

							glm::vec3 bc_coord{ ComputeBC(i + 0.5, j + 0.5, vso.pos) };

							if (bc_coord[0] < 0 || bc_coord[1] < 0 || bc_coord[2] < 0)
								continue; // not inside the triangle

							float zp{ bc_coord[0] * vso.pos[0].z + bc_coord[1] * vso.pos[1].z + bc_coord[2] * vso.pos[2].z };
							if (zp > depth_buffer[j * width + i])
								continue;							// depth test
							this->depth_buffer[j * width + i] = zp; // depth write

							// interp attribute

							float w = 1.0f / (vso.pos[0].w * bc_coord[0] + vso.pos[1].w * bc_coord[1] + vso.pos[2].w * bc_coord[2]);
							//std::cout << w << "\n";
							glm::vec3 interp_view_normal{ w * (vso.view_normal[0] * bc_coord[0] + vso.view_normal[1] * bc_coord[1] + vso.view_normal[2] * bc_coord[2]) };
							glm::vec3 interp_view_pos{ w * (vso.view_pos[0] * bc_coord[0] + vso.view_pos[1] * bc_coord[1] + vso.view_pos[2] * bc_coord[2]) };
							glm::vec2 interp_uv = { w * (vso.uv[0] * bc_coord[0] + vso.uv[1] * bc_coord[1] + vso.uv[2] * bc_coord[2]) };

							// pixel processing
							psin.view_pos = interp_view_pos;
							psin.view_normal = interp_view_normal;
							psin.uv = interp_uv;
							auto result_color = PixelShader(psin);
							SetPixel(i, j, result_color);

						}
					}

				}
			
		}
	}

	void Clear() {
		std::fill(color_buffer.begin(), color_buffer.end(), 0);
		std::fill(depth_buffer.begin(), depth_buffer.end(), 1.0f);
	}
	void* GetColorBuffer()
	{
		return color_buffer.data();
	}
	auto GetInfo() {
		return face_count, face_culled;
	}

private:


	bool CullBackFace(const vs_output& vso) {

		glm::vec3 v1{ glm::vec3(vso.pos[0]) };
		glm::vec3 v2{ glm::vec3(vso.pos[1]) };
		glm::vec3 v3{ glm::vec3(vso.pos[2]) };

		glm::vec3 ab{ v1 - v2 };
		glm::vec3 bc{ v2 - v3 };
		glm::vec3 view_normal(glm::cross(ab, bc));
		if (glm::dot(view_normal, glm::vec3(0, 0, 1)) <= 0) {
			face_culled++;
			return false;
		}
		return true;

	}
	bool Clipping(const vs_output& vso)
	{

		for (auto& i : vso.pos) {
			// check the triangle is inside the frustum
			if (i.x > i.w || i.x<-i.w || i.y>i.w || i.y<-i.w || i.z>i.w || i.z < -i.w) {
				return false;
			}
		}
		return true;
	}
	void PerspectiveDivision(vs_output& vso)
	{
		for (auto& i : vso.pos) {
			i.x /= i.w;
			i.y /= i.w;
			i.z /= i.w;
		}


	}
	void ScreenMapping(vs_output& vso)
	{
		for (auto& i : vso.pos) {
			i.z = 0.5 * i.z + 0.5;
			i.x = width * (i.x * 0.5 + 0.5);
			i.y = 0.5 * height - 0.5 * height * i.y;
		}
	}
	void PerspectiveCorrection(vs_output& vso) {

		for (uint32_t i = 0; i < 3; ++i) {
			vso.view_normal[i] /= vso.pos[i].w;
			vso.view_pos[i] /= vso.pos[i].w;
			vso.uv[i] /= vso.pos[i].w;
			vso.pos[i].w = 1.0f / vso.pos[i].w;
		}
	}

	glm::vec3 ComputeBC(float x, float y, std::array<glm::vec4, 3> tri)
	{
		glm::vec2 v0{ tri[1] - tri[0] };			  //b-a
		glm::vec2 v1{ tri[2] - tri[0] };			  //c-a
		glm::vec2 v2{ x - tri[0].x, y - tri[0].y }; //p-a

		//Vector v0 = b - a, v1 = c - a, v2 = p - a;
		float d00 = glm::dot(v0, v0);
		float d01 = glm::dot(v0, v1);
		float d11 = glm::dot(v1, v1);
		float d20 = glm::dot(v2, v0);
		float d21 = glm::dot(v2, v1);
		float denom = d00 * d11 - d01 * d01;
		float v = (d11 * d20 - d01 * d21) / denom;
		float w = (d00 * d21 - d01 * d20) / denom;
		float u = 1.0f - v - w;

		return {u, v, w};



	}

	void SetPixel(uint32_t x, uint32_t y, glm::vec3 color)
	{
		if (x > width || y > height)return;
		color_buffer[3 * (y * width + x) + 0] = static_cast<char>(255 * color[0]);
		color_buffer[3 * (y * width + x) + 1] = static_cast<char>(255 * color[1]);
		color_buffer[3 * (y * width + x) + 2] = static_cast<char>(255 * color[2]);
	}

private:
	uint32_t width{}, height{};
	std::vector<unsigned char> color_buffer{};
	std::vector<float> depth_buffer{};
	glm::mat4 view{}, projection{};
	std::vector<Model> models{};
	// pos, dir, color
	DirectLight light{ glm::vec4(0.0f,-0.3f,-1.0f,0.0f),2.0f * glm::vec3(1.0f,0.956f,0.8392f) };
	//PointLight plight{ glm::vec4(0.0f,5.0f,2.0f,1.0f),glm::vec3(1.0f,0.956f,0.8392f) };
	uint32_t  face_count{ 0 }, face_culled{ 0 };
};