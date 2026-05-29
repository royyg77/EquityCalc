"""Health-check route."""

from fastapi import APIRouter

router = APIRouter()


@router.get("/health")
async def health() -> dict[str, str]:
    """Return service status. Useful for readiness/liveness probes."""
    return {"status": "ok"}
