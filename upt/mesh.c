#include "upt/mesh.h"
#include "upt/bvh.h"
#include <float.h>
#include <string.h>

bool MeshTriangleBounds(Mesh const *this, size_t triangle, Aabb *bounds)
{
	for (int axis = 0; axis < 3; ++axis) {
		float lo = INFINITY, hi = -INFINITY;
		for (int j = 0; j < 3; ++j) {
			float3 p = this->p[this->indices[triangle][j]];
			float c = axis == 0 ? p.x : axis == 1 ? p.y : p.z;
			lo = fminf(lo, c);
			hi = fmaxf(hi, c);
		}
		bounds->corners[0][axis] = nextafterf(lo, -INFINITY);
		bounds->corners[1][axis] = nextafterf(hi, INFINITY);
	}
	return true;
}

MeshRay MeshRayPrepare(Ray const *ray)
{
	float d[3] = {ray->d.x, ray->d.y, ray->d.z};
	float o[3] = {ray->o.x, ray->o.y, ray->o.z};
	int kz = fabsf(d[0]) > fabsf(d[1]) ? 0 : 1;
	if (fabsf(d[2]) > fabsf(d[kz]))
		kz = 2;
	int kx = (kz + 1) % 3, ky = (kx + 1) % 3;
	return (MeshRay){.o = {o[kx], o[ky], o[kz]},
			 .sx = (double)d[kx] / d[kz],
			 .sy = (double)d[ky] / d[kz],
			 .dz = d[kz],
			 .axes = {kx, ky, kz}};
}

static bool triangle_intersect(Mesh const *this, size_t triangle, MeshRay const *ray, float tmin,
			       float tmax, TriangleHit *hit)
{
	// Keep symmetric edge products and double precision at shared edges.
	double x[3], y[3], z[3];
#ifdef __AVX__
	typedef float Coordinates __attribute__((vector_size(16)));
	typedef int Axes __attribute__((vector_size(16)));
	Axes axes;
	memcpy(&axes, ray->axes, sizeof(axes));
#endif
	for (int i = 0; i < 3; ++i) {
		float3 p = this->p[this->indices[triangle][i]];
#ifdef __AVX__
		Coordinates q = __builtin_ia32_vpermilvarps((Coordinates){p.x, p.y, p.z, 0}, axes);
		float px = q[0], py = q[1], pz = q[2];
#else
		float q[3] = {p.x, p.y, p.z};
		float px = q[ray->axes[0]], py = q[ray->axes[1]], pz = q[ray->axes[2]];
#endif
		double dz = (double)pz - ray->o[2];
		x[i] = ((double)px - ray->o[0]) - ray->sx * dz;
		y[i] = ((double)py - ray->o[1]) - ray->sy * dz;
		z[i] = dz / ray->dz;
	}
	double a = x[1] * y[2] - y[1] * x[2];
	double b = x[2] * y[0] - y[2] * x[0];
	double c = x[0] * y[1] - y[0] * x[1];
	if ((a < 0 || b < 0 || c < 0) && (a > 0 || b > 0 || c > 0))
		return false;
	double det = a + b + c;
	if (det == 0)
		return false;
	double t = (a * z[0] + b * z[1] + c * z[2]) / det;
	if (!(t >= tmin && t <= tmax))
		return false;
	if (hit)
		*hit = (TriangleHit){.t = t, .u = b / det, .v = c / det};
	return true;
}

TriangleHit MeshTriangleHit(Mesh const *mesh, size_t triangle, MeshRay const *ray, float tmin,
			    float tmax)
{
	TriangleHit hit = {0};
	hit.valid = triangle_intersect(mesh, triangle, ray, tmin, tmax, &hit);
	return hit;
}

bool MeshTriangleOccluded(Mesh const *mesh, size_t triangle, MeshRay const *ray, float tmin,
			  float tmax)
{
	return triangle_intersect(mesh, triangle, ray, tmin, tmax, NULL);
}

bool MeshHit(Shape *shape, HitRecord *hit, float tmin, float tmax)
{
	Mesh *this = (Mesh *)shape;
	if (!this->tree->Hit(this->tree, hit, tmin, tmax))
		return false;
	uint32_t const *v = this->indices[hit->primitive];
	float3 e1 = float3_sub(this->p[v[1]], this->p[v[0]]);
	float3 e2 = float3_sub(this->p[v[2]], this->p[v[0]]);
	hit->gn = hit->sn = float3_norm(float3_cross(e1, e2));
	float a = 1 - hit->u - hit->v;
	if (this->n) {
		float3 n = float3_add(float3_scale(this->n[v[0]], a),
				      float3_add(float3_scale(this->n[v[1]], hit->u),
						 float3_scale(this->n[v[2]], hit->v)));
		if (float3_dot(n, n) > 0)
			hit->sn = float3_norm(n);
		if (float3_dot(hit->sn, hit->gn) < 0)
			hit->sn = float3_scale(hit->sn, -1);
	}
	hit->dpdu = e1;
	hit->dpdv = e2;
	if (this->uv) {
		float du1 = this->uv[v[1]][0] - this->uv[v[0]][0];
		float dv1 = this->uv[v[1]][1] - this->uv[v[0]][1];
		float du2 = this->uv[v[2]][0] - this->uv[v[0]][0];
		float dv2 = this->uv[v[2]][1] - this->uv[v[0]][1];
		float det = du1 * dv2 - dv1 * du2;
		if (fabsf(det) > 1e-12f) {
			hit->dpdu = float3_scale(
			    float3_sub(float3_scale(e1, dv2), float3_scale(e2, dv1)), 1 / det);
			hit->dpdv = float3_scale(
			    float3_sub(float3_scale(e2, du1), float3_scale(e1, du2)), 1 / det);
		}
		float u = hit->u, vweight = hit->v;
		hit->u =
		    a * this->uv[v[0]][0] + u * this->uv[v[1]][0] + vweight * this->uv[v[2]][0];
		hit->v =
		    a * this->uv[v[0]][1] + u * this->uv[v[1]][1] + vweight * this->uv[v[2]][1];
	}
	hit->object = hit->leaf = shape;
	hit->material = this->material;
	return true;
}

bool MeshBounds(Shape *shape, Aabb *bounds)
{
	return BvhBounds(((Mesh *)shape)->tree, bounds);
}

Mesh *MeshCreate(float3 *p, float3 *n, float (*uv)[2], uint32_t (*indices)[3], size_t nvertices,
		 size_t ntriangles, Material *material)
{
	Mesh *this = ALLOC_SHAPE(Mesh);
	*this = (Mesh){.base = this->base,
		       .material = material,
		       .p = p,
		       .n = n,
		       .uv = uv,
		       .indices = indices,
		       .nvertices = nvertices,
		       .ntriangles = ntriangles};
	this->tree = BvhBuildMesh(this);
	return this;
}

void MeshFree(void *ptr)
{
	Mesh *this = ptr;
	BvhFree(this->tree);
	free(this->p);
	free(this->n);
	free(this->uv);
	free(this->indices);
	free(this);
}

Mesh *MeshReadPly(char const *path, Material *material)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		perror(path);
		exit(EXIT_FAILURE);
	}
	char line[1024], element[32] = "";
	size_t nv = 0, nt = 0;
	int properties = 0, xyz[3] = {-1, -1, -1}, normal[3] = {-1, -1, -1}, texcoord[2] = {-1, -1};
	bool ascii = false, header = false, faces = false;
	if (!fgets(line, sizeof(line), f) || strcmp(line, "ply\n"))
		goto invalid;
	while (fgets(line, sizeof(line), f)) {
		char type[32], name[32];
		size_t count;
		if (!strcmp(line, "format ascii 1.0\n"))
			ascii = true;
		else if (sscanf(line, "element %31s %zu", name, &count) == 2) {
			strcpy(element, name);
			if (!strcmp(name, "vertex") && !nv && !nt)
				nv = count;
			else if (!strcmp(name, "face") && nv && !nt)
				nt = count;
			else
				goto invalid;
		} else if (sscanf(line, "property %31s %31s", type, name) == 2) {
			if (!strcmp(element, "vertex")) {
				if (!strcmp(type, "list"))
					goto invalid;
				for (int a = 0; a < 3; ++a)
					if (name[0] == "xyz"[a] && !name[1])
						xyz[a] = properties;
				for (int a = 0; a < 3; ++a)
					if (name[0] == 'n' && name[1] == "xyz"[a] && !name[2])
						normal[a] = properties;
				if (!strcmp(name, "s") || !strcmp(name, "u"))
					texcoord[0] = properties;
				if (!strcmp(name, "t") || !strcmp(name, "v"))
					texcoord[1] = properties;
				++properties;
			} else if (!strcmp(element, "face") && !faces &&
				   !strcmp(line, "property list uchar int vertex_indices\n"))
				faces = true;
			else
				goto invalid;
		} else if (!strcmp(line, "end_header\n")) {
			header = true;
			break;
		}
	}
	if (!ascii || !header || !faces || !nv || !nt || nv > UINT32_MAX ||
	    nv > SIZE_MAX / sizeof(float3) || nt > SIZE_MAX / sizeof(uint32_t[3]) || xyz[0] < 0 ||
	    xyz[1] < 0 || xyz[2] < 0)
		goto invalid;
	bool has_normals = normal[0] >= 0 && normal[1] >= 0 && normal[2] >= 0;
	bool has_uv = texcoord[0] >= 0 && texcoord[1] >= 0;
	float3 *p = xmalloc(nv * sizeof(*p)), *n = xcalloc(nv, sizeof(*n));
	float(*uv)[2] = has_uv ? xmalloc(nv * sizeof(*uv)) : NULL;
	uint32_t(*indices)[3] = xmalloc(nt * sizeof(*indices));
	for (size_t i = 0; i < nv; ++i) {
		float v[3] = {0}, nn[3] = {0}, st[2] = {0};
		for (int j = 0; j < properties; ++j) {
			float value;
			if (fscanf(f, "%f", &value) != 1 || !isfinite(value))
				goto invalid;
			for (int a = 0; a < 3; ++a) {
				if (j == xyz[a])
					v[a] = value;
				if (j == normal[a])
					nn[a] = value;
			}
			for (int a = 0; a < 2; ++a)
				if (j == texcoord[a])
					st[a] = value;
		}
		p[i] = (float3){v[0], v[1], v[2]};
		if (has_normals)
			n[i] = (float3){nn[0], nn[1], nn[2]};
		if (has_uv) {
			uv[i][0] = st[0];
			uv[i][1] = st[1];
		}
	}
	for (size_t i = 0; i < nt; ++i) {
		unsigned count, a, b, c;
		if (fscanf(f, "%u %u %u %u", &count, &a, &b, &c) != 4 || count != 3 || a >= nv ||
		    b >= nv || c >= nv)
			goto invalid;
		indices[i][0] = a;
		indices[i][1] = b;
		indices[i][2] = c;
		if (!has_normals) {
			float3 normal =
			    float3_cross(float3_sub(p[b], p[a]), float3_sub(p[c], p[a]));
			n[a] = float3_add(n[a], normal);
			n[b] = float3_add(n[b], normal);
			n[c] = float3_add(n[c], normal);
		}
	}
	for (size_t i = 0; i < nv; ++i)
		if (float3_dot(n[i], n[i]) > 0)
			n[i] = float3_norm(n[i]);
	fclose(f);
	return MeshCreate(p, n, uv, indices, nv, nt, material);
invalid:
	fprintf(stderr, "%s: expected a finite ASCII PLY triangle mesh\n", path);
	fclose(f);
	exit(EXIT_FAILURE);
}

bool MeshOccluded(Shape *shape, Ray const *ray, float tmin, float tmax)
{
	Mesh *this = (Mesh *)shape;
	return ShapeOccluded(this->tree, ray, tmin, tmax);
}
