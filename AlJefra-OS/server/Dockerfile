FROM python:3.12-slim

LABEL maintainer="AlJefra OS <dev@aljefra.com>"
LABEL description="AlJefra Driver Marketplace API Server"

WORKDIR /app

# Install dependencies
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy server code
COPY app.py models.py driver_store.py ajdrv_builder.py ./

# Copy seed drivers and catalog (if present)
COPY drivers/ drivers/
COPY catalog.json* ./

# Expose API port
EXPOSE 8080

# Environment
ENV ALJEFRA_HOST=0.0.0.0
ENV ALJEFRA_PORT=8080

# Health check
HEALTHCHECK --interval=30s --timeout=5s --retries=3 \
    CMD python -c "import urllib.request; urllib.request.urlopen('http://localhost:8080/')"

# Run with gunicorn in production
CMD ["gunicorn", "--bind", "0.0.0.0:8080", "--workers", "4", "--timeout", "120", "app:app"]
