#!/usr/bin/env python3

import asyncio
from pathlib import Path
import subprocess

import asyncclick as click
from golioth import Client


SCRIPTS_DIR = Path(__file__).resolve().parents[0]
SAMPLE_DIR = SCRIPTS_DIR.parents[0]
KEYS_DIR = SAMPLE_DIR / 'keys'

ROOT_CA_NAME = 'root-ca'


def run(*args, **kwargs):
    if 'check' not in kwargs:
        kwargs['check'] = True

    if 'shell' not in kwargs:
        kwargs['shell'] = True

    print(f'Running: {args}')
    return subprocess.run(*args, **kwargs)


@click.command()
@click.option('-c', '--config-path', type=Path,
              help='Path to goliothctl configuration')
@click.option('--api-key', help='API key')
@click.option('-d', '--device-name',
              help='Name of device on which RPC method will be called',
              required=True)
async def main(config_path, api_key, device_name):
    """Generate certificates, add them to Golioth and populate to keys/ directory in sample."""
    client = Client(config_path, api_key=api_key)
    project = await client.default_project()
    device = await project.device_by_name(device_name)

    primary_hardware_id = device.info['hardwareIds'][0]
    client_name = f"{project.id}-{device.name}"

    root_ca_crt = SCRIPTS_DIR / f'{ROOT_CA_NAME}.crt.pem'
    root_ca_key = SCRIPTS_DIR / f'{ROOT_CA_NAME}.key.pem'

    # Generate an elliptic curve private key
    # Run `openssl ecparam -list_curves` to list all available algorithms
    # Keep this key safe! Anyone who has it can sign authentic-looking device certificates
    run(f'openssl ecparam -name prime256v1 -genkey -noout -out "{root_ca_key}"')

    # Create and self-sign a corresponding public key / certificate
    run(f'openssl req -x509 -new -nodes -key "{root_ca_key}" -sha256 -subj "/C=US/CN=Root {ROOT_CA_NAME}" -days 1024 -out "{root_ca_crt}"')

    # Generate an elliptic curve private key
    run(f'openssl ecparam -name prime256v1 -genkey -noout -out "{client_name}.key.pem"')

    # Create a certificate signing request (CSR)
    # (this is what you would normally give to your CA / PKI to sign)
    run(f'openssl req -new -key "{client_name}.key.pem" -subj "/C=US/O={project.id}/CN={primary_hardware_id}" -out "{client_name}.csr.pem"')

    # Sign the certificate (CSR) using the previously generated self-signed root certificate
    run(f'openssl x509 -req \
        -in "{client_name}.csr.pem" \
        -CA "{root_ca_crt}" \
        -CAkey "{root_ca_key}" \
        -CAcreateserial \
        -out "{client_name}.crt.pem" \
        -days 500 -sha256')

    # Add (upload) generated CA certificate to Golioth
    with root_ca_crt.open('rb') as fp:
        await project.certificates.add(fp.read(), cert_type='root')

    # Convert keys/certificates to DER form
    KEYS_DIR.mkdir(exists_ok=True)
    run(f'openssl x509 -in {client_name}.crt.pem -outform DER -out {KEYS_DIR}/{client_name}.crt.der')
    run(f'openssl ec -in {client_name}.key.pem -outform DER -out {KEYS_DIR}/{client_name}.key.der')


if __name__ == '__main__':
    main()
