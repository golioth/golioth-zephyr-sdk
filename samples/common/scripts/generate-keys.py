#!/usr/bin/env python3

from pathlib import Path
import subprocess

import asyncclick as click
from golioth import Client, Project

ROOT_CA_NAME = 'root-ca'

def run(*args, **kwargs):
    if 'check' not in kwargs:
        kwargs['check'] = True

    if 'shell' not in kwargs:
        kwargs['shell'] = True

    return subprocess.run(*args, **kwargs)

@click.group()
async def cli():
    pass

@click.command()
@click.option('--upload/--no-upload', default=False, help='Upload root certificate to Golioth')
@click.option('-c', '--config-path', type=Path,
              help='Path to goliothctl configuration')
@click.option('--api-key', help='API key')
@click.option('--project-id', help='Project ID for the project where root certificate will be uploaded')
@click.option('-o', '--output', type=Path, default=Path('.'), help="Output directory")
async def root(upload, config_path, api_key, project_id, output):
    if upload:
        if not api_key:
            click.echo("API key required to upload root certificate")

        client = Client(config_path, api_key=api_key)

        if project_id:
            project = await Project.get_by_id(client, project_id)
        else:
            project = await client.default_project()

    root_ca_crt = output / f'{ROOT_CA_NAME}-{project_id}.crt.pem'
    root_ca_key = output / f'{ROOT_CA_NAME}-{project_id}.key.pem'

    # Generate an elliptic curve private key
    # Run `openssl ecparam -list_curves` to list all available algorithms
    # Keep this key safe! Anyone who has it can sign authentic-looking device certificates
    run(f'openssl ecparam -name prime256v1 -genkey -noout -out "{root_ca_key}"')

    # Create and self-sign a corresponding public key / certificate
    run(f'openssl req -x509 -new -nodes -key "{root_ca_key}" -sha256 -subj "/C=US/CN=Root {ROOT_CA_NAME}" -days 1024 -out "{root_ca_crt}"')

    if upload:
        # Add (upload) generated CA certificate to Golioth
        with root_ca_crt.open('rb') as fp:
            await project.certificates.add(fp.read(), cert_type='root')

@click.command()
@click.option('--device-name', help="This will be the Device Name in the Golioth Console (e.g. a serial number)",
              required=True)
@click.option('--project-id', help="Project ID", required=True)
@click.option('--root-ca-crt', type=Path, help="Root certificate that will sign device certificate",
              required=True)
@click.option('--root-ca-key', type=Path, help="Root private key that will sign device certificate",
              required=True)
@click.option('-o', '--output', type=Path, default=Path('.'), help="Output directory")
async def device(device_name, project_id, root_ca_crt, root_ca_key, output):
    client_path_stem = output / f"{project_id}-{device_name}"

    # Generate an elliptic curve private key
    run(f'openssl ecparam -name prime256v1 -genkey -noout -out "{client_path_stem}.key.pem"')

    # Create a certificate signing request (CSR)
    # (this is what you would normally give to your CA / PKI to sign)
    run(f'openssl req -new -key "{client_path_stem}.key.pem" -subj "/C=US/O={project_id}/CN={device_name}" -out "{client_path_stem}.csr.pem"')

    # Sign the certificate (CSR) using the previously generated self-signed root certificate
    run(f'openssl x509 -req \
        -in "{client_path_stem}.csr.pem" \
        -CA "{root_ca_crt}" \
        -CAkey "{root_ca_key}" \
        -CAcreateserial \
        -out "{client_path_stem}.crt.pem" \
        -days 500 -sha256')

    run(f'openssl x509 -in {client_path_stem}.crt.pem -outform DER -out {client_path_stem}.crt.der')
    run(f'openssl ec -in {client_path_stem}.key.pem -outform DER -out {client_path_stem}.key.der')

cli.add_command(root)
cli.add_command(device)

if __name__ == '__main__':
    cli()
