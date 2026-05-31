# Run Interactive Container

Run a Docker container interactively.

## Steps

1. Pull the image:
   {{tool:system_docker_pull args='["IMAGE"]'}}

2. Run the container:
   {{tool:system_docker_run args='["--rm", "-it", "IMAGE"]'}}

## Report
Summarize: what image was used, any output.
