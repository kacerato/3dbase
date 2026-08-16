from pathlib import Path
p=Path('src/app/qt/vulkan_viewport.cpp')
s=p.read_text()
old='''[[nodiscard]] QPointF normalizedPoint(QPointF value) noexcept {\n    const float length = pointLength(value);\n    if (length <= 1.0e-5F) return {};\n    return value / static_cast<qreal>(length);\n}\n\n'''
if s.count(old)!=1: raise SystemExit('unused helper anchor mismatch')
p.write_text(s.replace(old,'',1))
print('removed unused normalizedPoint helper')
