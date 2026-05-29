"""FastAPI application entry point for EquityCalc backend.

Run locally:
    uvicorn app.main:app --reload --port 8000
"""

from fastapi import FastAPI

from app.routes import health

app = FastAPI(
    title="EquityCalc API",
    version="0.1.0-dev",
    description="Poker equity calculations over HTTP and WebSocket.",
)

app.include_router(health.router)

# TODO: register equity routes once engine bindings are available.
# app.include_router(equity.router, prefix="/api")
