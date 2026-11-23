"""Small deterministic geometry assets for the underwater composition."""
from math import sin, cos, pi


def build_underwater(assets):
    def write(name, vertices, faces):
        with (assets/name).open("w") as f:
            f.write(f"ply\nformat ascii 1.0\nelement vertex {len(vertices)}\n")
            for p in ["x","y","z","s","t"]:
                f.write("property float "+p+"\n")
            f.write(f"element face {len(faces)}\nproperty list uchar int vertex_indices\nend_header\n")
            for v in vertices: f.write(" ".join(f"{x:.7g}" for x in v)+"\n")
            for tri in faces: f.write("3 "+" ".join(map(str,tri))+"\n")

    n=256
    vertices=[]
    for z in range(n+1):
        for x in range(n+1):
            px=-30+60*x/n; pz=-15+70*z/n
            y=-.35+.1*sin(.7*px+.16*pz)+.08*cos(.55*pz-.2*px)
            y+=.025*sin(12*px+1.5*sin(.8*pz))
            vertices.append((px,y,pz,px/3,pz/3))
    faces=[]
    for z in range(n):
        for x in range(n):
            a=z*(n+1)+x; b=a+1; c=a+n+2; d=a+n+1
            faces.extend([(a,d,c),(a,c,b)])
    write("seabed.ply",vertices,faces)

    # Weathered closed rock: latitude rings with deterministic multi-scale relief.
    n,m=64,32
    vertices=[(0,1,0,.5,0)]
    for j in range(1,m):
        theta=pi*j/m
        for i in range(n):
            phi=2*pi*i/n
            r=1+.12*sin(3*phi+cos(4*theta))+.07*sin(7*phi-5*theta)+.035*cos(13*phi+8*theta)
            vertices.append((r*sin(theta)*cos(phi),r*cos(theta),r*sin(theta)*sin(phi),i/n,j/m))
    bottom=len(vertices); vertices.append((0,-1,0,.5,1))
    faces=[]
    for i in range(n):
        a=1+i; b=1+(i+1)%n
        faces.append((0,b,a))
    for j in range(m-2):
        for i in range(n):
            a=1+j*n+i; b=1+j*n+(i+1)%n; c=b+n; d=a+n
            faces.extend([(a,b,c),(a,c,d)])
    start=1+(m-2)*n
    for i in range(n): faces.append((bottom,start+i,start+(i+1)%n))
    write("reef-rock.ply",vertices,faces)

    # A cluster of curved, tapered kelp ribbons, instanced around the reef.
    vertices=[];faces=[]
    for leaf in range(12):
        angle=2*pi*leaf/12
        height=.9+.9*(.5+.5*sin(leaf*1.7))
        start=len(vertices)
        for j in range(33):
            t=j/32
            radius=.08+.24*t+.16*sin(3*t+leaf)
            x=cos(angle)*radius+.13*t*t*sin(leaf+3*t)
            z=sin(angle)*radius+.12*t*t*cos(leaf+4*t)
            width=.055*sin(pi*t)**.65+.002
            for side in [-1,1]:
                vertices.append((x+side*width*cos(angle+.8*t),height*t,z+side*width*sin(angle+.8*t),.5+.5*side,t))
            if j:
                a=start+2*(j-1);faces.extend([(a,a+2,a+3),(a,a+3,a+1)])
    write("kelp.ply",vertices,faces)
