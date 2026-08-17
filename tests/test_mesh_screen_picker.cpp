#include "test_harness.hpp"
#include "mobile3d/editor/mesh_screen_picker.hpp"
#include <vector>

TEST_CASE("mesh screen picker chooses nearest visible vertex in pixel space") {
    const std::vector<m3d::MeshScreenVertex> vertices{{{1U}, {100,100,0.60F}}, {{2U}, {105,100,0.30F}}, {{3U}, {150,150,0.10F}}};
    m3d::MeshScreenPickRequest request; request.mode=m3d::MeshSelectionMode::Vertex; request.x=103; request.y=100; request.vertexRadius=12;
    const auto hit=m3d::MeshScreenPicker::pick(vertices,{}, {},request); REQUIRE(hit.has_value()); REQUIRE(hit->elementId==2U);
}
TEST_CASE("mesh screen picker resolves edge distance and depth ties deterministically") {
    const std::vector<m3d::MeshScreenEdge> edges{{{4U},{50,50,0.70F},{150,50,0.70F}},{{5U},{50,54,0.25F},{150,54,0.25F}}};
    m3d::MeshScreenPickRequest request; request.mode=m3d::MeshSelectionMode::Edge; request.x=100; request.y=52; request.edgeTolerance=8;
    const auto hit=m3d::MeshScreenPicker::pick({},edges,{},request); REQUIRE(hit.has_value()); REQUIRE(hit->elementId==5U);
}
TEST_CASE("mesh screen picker chooses frontmost overlapping face") {
    const std::vector<m3d::MeshScreenFace> faces{{{10U},{{0,0,0.70F},{100,0,0.70F},{100,100,0.70F},{0,100,0.70F}}},{{11U},{{0,0,0.20F},{100,0,0.20F},{100,100,0.20F},{0,100,0.20F}}}};
    m3d::MeshScreenPickRequest request; request.mode=m3d::MeshSelectionMode::Face; request.x=40; request.y=60;
    const auto hit=m3d::MeshScreenPicker::pick({}, {},faces,request); REQUIRE(hit.has_value()); REQUIRE(hit->elementId==11U);
}
TEST_CASE("mesh screen picker rejects component hidden behind front surface") {
    const std::vector<m3d::MeshScreenVertex> vertices{{{20U},{50,50,0.80F}}};
    const std::vector<m3d::MeshScreenFace> faces{{{21U},{{0,0,0.20F},{100,0,0.20F},{100,100,0.20F},{0,100,0.20F}}}};
    m3d::MeshScreenPickRequest request; request.mode=m3d::MeshSelectionMode::Vertex; request.x=50; request.y=50; request.vertexRadius=12;
    REQUIRE(!m3d::MeshScreenPicker::pick(vertices,{},faces,request).has_value());
}
