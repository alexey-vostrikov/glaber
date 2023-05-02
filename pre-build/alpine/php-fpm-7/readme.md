## Build the Docker image

To build the glaber-php-fpm Docker image, run the following command in the project directory:

```bash
docker build -t glaber-php-fpm .
```

This command uses the Dockerfile in the project directory to build the Docker image and tags it with the name glaber-php-fpm.

## Save the Docker image

To save the glaber-php-fpm Docker image as a compressed tar archive, run the following command:
```bash
docker save glaber-php-fpm | gzip > glaber-php-fpm.tgz
```

This command uses the docker save command to save the Docker image as a tar archive, pipes the output to the gzip command to compress it, and redirects the compressed output to a file named glaber-php-fpm.tgz.

## Load the Docker image

To load the glaber-php-fpm Docker image from the compressed tar archive, run the following command:

```bash
gunzip -c glaber-php-fpm.tgz | docker load
```

This command uses the gunzip command to decompress the glaber-php-fpm.tgz file and pipes the output to the docker load command to load the Docker image into the local Docker repository.

## Run the Docker container

To run the glaber-php-fpm Docker container, first create a directory on the host machine to store the PHP files that will be served by the container:

```bash
mkdir -p /var/www/ui
```
Then copy the PHP files to the /var/www/ui directory on the host machine.

Next, set the ownership of the /var/www/ui directory to the user and group ID of the www-data user inside the container (UID and GID 1101):

```bash
chown -R 1101:1101 /var/www/ui
```

Finally, run the glaber-php-fpm Docker container with the following command:

```bash
docker run -d -p 9000:9000 -v /var/www/ui:/var/www/ui glaber-php-fpm
```

This command runs the glaber-php-fpm Docker container in detached mode (-d), maps port 9000 of the container to port 9000 of the host machine (-p 9000:9000), and mounts the /var/www/ui directory on the host machine as a volume inside the container (-v /var/www/ui:/var/www/ui). This allows the PHP files in the /var/www/ui directory on the host machine to be served by the PHP-FPM process inside the container.