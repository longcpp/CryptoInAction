use bellman::{
    gadgets::{
        boolean::{AllocatedBit, Boolean},
        multipack,
        sha256::sha256,
    },
    groth16, Circuit, ConstraintSystem, SynthesisError,
};
use pairing::{bls12_381::Bls12, Engine};
use rand::{thread_rng, Rng};
use sha2::{Digest, Sha256};
use std::time::SystemTime;

struct Sha256Preimage {
    preimage: Vec<u8>,
}

impl<E: Engine> Circuit<E> for Sha256Preimage {
    fn synthesize<CS: ConstraintSystem<E>>(self, cs: &mut CS) -> Result<(), SynthesisError> {
        let mut preimage_bits: Vec<Boolean> = [].to_vec();

        for (byte_i, preimage_byte) in self.preimage.into_iter().enumerate() {
            for bit_i in (0..8).rev() {
                let cs = cs.namespace(|| format!("preimage bit {} {}", byte_i, bit_i));
                preimage_bits.push(
                    AllocatedBit::alloc(cs, Some((preimage_byte >> bit_i) & 1u8 == 1u8))
                        .unwrap()
                        .into(),
                );
            }
        }

        let digest = sha256(cs.namespace(|| "sha256(preimage)"), &preimage_bits).unwrap();
        multipack::pack_into_inputs(cs, &digest)
    }
}

fn eval_sha256_circuit<E: Engine>(num_bytes: usize) {
    let random_bytes: Vec<u8> = (0..num_bytes).map(|_| thread_rng().gen()).collect();
    let params = {
        let c = Sha256Preimage {
            preimage: random_bytes,
        };
        println!("generate_random_parameters");
        groth16::generate_random_parameters::<E, _, _>(c, &mut thread_rng()).unwrap()
    };

    println!("prepare_verifying_key");
    let pvk = groth16::prepare_verifying_key(&params.vk);

    let preimage_bytes: Vec<u8> = (0..num_bytes).map(|_| thread_rng().gen()).collect();
    let digest = Sha256::digest(&preimage_bytes);
    let preimage = Sha256Preimage {
        preimage: preimage_bytes,
    };

    println!("create_random_proof");
    let start = SystemTime::now();
    let proof = groth16::create_random_proof(preimage, &params, &mut thread_rng()).unwrap();
    println!(
        "prover time: {:?}",
        SystemTime::now().duration_since(start).unwrap()
    );

    let digest_bits = multipack::bytes_to_bits(&digest);
    let inputs = multipack::compute_multipacking::<E>(&digest_bits);

    println!("verify_proof");
    let start = SystemTime::now();
    assert!(
        groth16::verify_proof(&pvk, &proof, &inputs).unwrap(),
        "correct!!"
    );
    println!(
        "verifier time: {:?}",
        SystemTime::now().duration_since(start).unwrap()
    );
}

fn main() {
    const NUM_HASHES: usize = 4;
    for i in 0..NUM_HASHES {
        let num_bytes = (i + 1) * 64;
        println!("hashing {:?} bytes", num_bytes);
        eval_sha256_circuit::<Bls12>(num_bytes);
    }
}
