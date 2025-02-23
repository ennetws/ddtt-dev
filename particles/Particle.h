#pragma once

#include <QOpenGLShaderProgram>
#include <QWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QFileDialog>
#include <QElapsedTimer>

#include <vector>
#include <stdint.h>
#include <Eigen/Core>

enum ParticleFlags{ NONE, FLOOR, UNPROCESSED, VIZ_WEIGHT };

#include "Serializable.h"

template<typename Vector3>
struct Particle : public Serializable
{
	typedef double Scalar;

    explicit Particle(const Vector3& pos = Vector3(0,0,0)) : pos(pos), measure(0.0), weight(1), 
		alpha(1.0), direction(Vector3(0,0,1)), flag(NONE), avgDiameter(0), segment(0), neighbour(-1), isMedial(false), medialID(-1)
	{
		id = -1; // an invalid ID
		correspondence = -1;
		isMatched = false;
	}

	size_t id, correspondence;
	uint64_t morton;
	Vector3 pos, direction, axis, relativePos;
	ParticleFlags flag;
	int segment, neighbour;
    Scalar measure, weight, alpha;
	Scalar avgDiameter, flat;

	size_t medialID;
	bool isMedial;
	Vector3 medialPos;

	bool isMatched;

	// Serialization:
	void serialize(QDataStream& os) const {
		os << id << correspondence << morton << ((qint32)flag) << segment << neighbour;
		os << pos << direction << axis;
		os << measure << weight << alpha;
		os << avgDiameter << flat;
		os << medialID;
		os << isMedial;
		os << medialPos;
	}	
	void deserialize(QDataStream& is) {
		int flagItem;
		is >> id >> correspondence >> morton >> flagItem >> segment >> neighbour;
		is >> pos >> direction >> axis;
		is >> measure >> weight >> alpha;
		is >> avgDiameter >> flat;
		is >> medialID;
		is >> isMedial;
		is >> medialPos;
		flag = (ParticleFlags)flagItem;
	}
};
