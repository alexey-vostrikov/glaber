#!/bin/sh
# https://semver.org/spec/v2.0.0.html

# variables
GITLAB_API_URL="https://gitlab.com/api/v4/projects/${CI_PROJECT_ID}/registry/repositories"
SEARCH_PATH="${CI_PROJECT_NAMESPACE}/${CI_PROJECT_NAME}/${CI_JOB_NAME}"
JQ_URL="https://github.com/stedolan/jq/releases/download/jq-1.6/jq-linux64"

# Install jq
wget -q --no-check-certificate -O /busybox/jq ${JQ_URL}
chmod +x /busybox/jq

# Send GET request to GitLab API to retrieve repository details
REPO_DETAILS=$(wget --no-check-certificate -q -O - "${GITLAB_API_URL}" | \
               jq --arg path "${SEARCH_PATH}" '.[] | select(.path == $path)')

# Extract repository ID from repository details
REPO_ID=$(echo "${REPO_DETAILS}" | jq -r '.id')

# Make kaniko build function
kaniko_build() {
  /kaniko/executor --context "${BUILD_DIR}" \
                   --build-arg OS=${OS} \
                   --build-arg OS_VER=${OS_VER} \
                   --dockerfile "${BUILD_DIR}/Dockerfile" \
                   --destination "${BUILD_IMG}:${BUILD_TAG}" \
                   --cache=true \
                   --cache-repo="${CACHE_REPO}" \
                   --cleanup \
                   --use-new-run \
                   --snapshot-mode=redo \
                   --verbosity=trace
}

# Build image if docker repository not exist
if [ -z "$REPO_ID" ] ; then
  echo "Build and push ${CI_JOB_NAME}:${BUILD_TAG} with kaniko"
  kaniko_build
else
  # Set image repository URL
  IMAGE_REPO_URL="${GITLAB_API_URL}/${REPO_ID}"

  # Check if docker image already exists
  if [[ "$(wget --no-check-certificate -q -O - "${IMAGE_REPO_URL}/tags/${BUILD_TAG}" | jq -r '.name')" == "${BUILD_TAG}" ]]
  then
    echo "Docker image ${CI_JOB_NAME}:${BUILD_TAG} already exists, skipping the build"
  else
    echo "Build and push ${CI_JOB_NAME}:${BUILD_TAG} with kaniko"
    kaniko_build
  fi
fi
