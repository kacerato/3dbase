from pathlib import Path
p=Path('src/editor/src/transform_manipulator.cpp')
s=p.read_text()
old='''[[nodiscard]] Quat multiplied(Quat left, Quat right) noexcept {\n    return normalized({\n        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,\n        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,\n        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,\n        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,\n    });\n}\n'''
new='''[[nodiscard]] Quat multipliedRaw(Quat left, Quat right) noexcept {\n    return {\n        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,\n        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,\n        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,\n        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,\n    };\n}\n\n[[nodiscard]] Quat multiplied(Quat left, Quat right) noexcept {\n    return normalized(multipliedRaw(left, right));\n}\n'''
if s.count(old)!=1: raise SystemExit('quaternion multiply anchor mismatch')
s=s.replace(old,new,1)
old='''    const Quat result = multiplied(multiplied(q, vector), conjugated(q));\n'''
new='''    const Quat result = multipliedRaw(multipliedRaw(q, vector), conjugated(q));\n'''
if s.count(old)!=1: raise SystemExit('vector rotation anchor mismatch')
s=s.replace(old,new,1)
p.write_text(s)
print('fixed quaternion vector multiplication without normalization')
