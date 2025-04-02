namespace Beefy.geom;

struct Oval
{
	public float mX;
	public float mY;
	public float mRadiusX;
	public float mRadiusY;

	public this(float x, float y, float radiusX, float radiusY)
	{
		mX = x;
		mY = y;
		mRadiusX = radiusX;
		mRadiusY = radiusY;
	}

	public this(Rect rect)
	{
		mX = rect.CenterX;
		mY = rect.CenterY;
		mRadiusX = rect.mWidth / 2;
		mRadiusY = rect.mHeight / 2;
	}

	public bool Contains(float x, float y)
	{
		float dx = (x - mX) / mRadiusX;
		float dy = (y - mY) / mRadiusY;
		return dx*dx + dy*dy <= 1.0f;
	}

	public void Inflate(float x, float y) mut
	{
		mRadiusX += x;
		mRadiusY += y;
	}

	public bool Contains(Vector2 vec) => Contains(vec.mX, vec.mY);
}