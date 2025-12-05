#!/bin/bash
set -e

echo "🚀 Deploying AWS Wildfire Detection System - Phase 1: Data Ingestion"
echo "=================================================================="
echo ""

# Variables
REGION="us-east-2"
STACK_PREFIX="wildfire-sensor"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CF_DIR="${SCRIPT_DIR}/../cloudformation"

echo "Configuration:"
echo "  Region: $REGION"
echo "  Stack Prefix: $STACK_PREFIX"
echo ""

# Phase 1.1: Deploy IAM Roles (must be first)
echo "Step 1/4: Deploying IAM Roles..."
aws cloudformation deploy \
  --template-file "${CF_DIR}/04-iam-roles.yaml" \
  --stack-name ${STACK_PREFIX}-iam-roles \
  --capabilities CAPABILITY_NAMED_IAM \
  --region $REGION \
  --no-fail-on-empty-changeset

if [ $? -eq 0 ]; then
    echo "✅ IAM Roles deployed successfully"
else
    echo "❌ Failed to deploy IAM Roles"
    exit 1
fi
echo ""

# Phase 1.2: Deploy Timestream Database
echo "Step 2/4: Deploying Timestream Database..."
aws cloudformation deploy \
  --template-file "${CF_DIR}/01-timestream.yaml" \
  --stack-name ${STACK_PREFIX}-timestream \
  --region $REGION \
  --no-fail-on-empty-changeset

if [ $? -eq 0 ]; then
    echo "✅ Timestream Database deployed successfully"
else
    echo "❌ Failed to deploy Timestream"
    exit 1
fi
echo ""

# Phase 1.3: Deploy S3 Archive Bucket
echo "Step 3/4: Deploying S3 Archive Bucket..."
aws cloudformation deploy \
  --template-file "${CF_DIR}/03-s3.yaml" \
  --stack-name ${STACK_PREFIX}-s3 \
  --region $REGION \
  --no-fail-on-empty-changeset

if [ $? -eq 0 ]; then
    echo "✅ S3 Archive Bucket deployed successfully"
else
    echo "❌ Failed to deploy S3 Bucket"
    exit 1
fi
echo ""

# Phase 1.4: Create IoT Rules
echo "Step 4/4: Creating IoT Rules..."
"${SCRIPT_DIR}/create-iot-rules.sh"

echo ""
echo "=================================================================="
echo "✅ Phase 1 Deployment Complete!"
echo "=================================================================="
echo ""
echo "📊 Deployment Summary:"
echo ""

# Get stack outputs
echo "Timestream Database:"
DB_NAME=$(aws cloudformation describe-stacks --stack-name ${STACK_PREFIX}-timestream --region $REGION --query 'Stacks[0].Outputs[?OutputKey==`DatabaseName`].OutputValue' --output text)
TABLE_NAME=$(aws cloudformation describe-stacks --stack-name ${STACK_PREFIX}-timestream --region $REGION --query 'Stacks[0].Outputs[?OutputKey==`TableName`].OutputValue' --output text)
echo "  Database: $DB_NAME"
echo "  Table: $TABLE_NAME"
echo ""

echo "S3 Archive Bucket:"
BUCKET_NAME=$(aws cloudformation describe-stacks --stack-name ${STACK_PREFIX}-s3 --region $REGION --query 'Stacks[0].Outputs[?OutputKey==`BucketName`].OutputValue' --output text)
echo "  Bucket: $BUCKET_NAME"
echo ""

echo "IoT Rules:"
aws iot list-topic-rules --region $REGION --query 'rules[?starts_with(ruleName, `Sensor`)].ruleName' --output table
echo ""

echo "=================================================================="
echo "🧪 Testing Phase 1"
echo "=================================================================="
echo ""
echo "Your ESP32 Border Router should now be able to send data to AWS."
echo "Data will flow to:"
echo "  1. Timestream (real-time queries)"
echo "  2. S3 (long-term archive)"
echo ""
echo "To verify data is arriving:"
echo ""
echo "1. Check Timestream:"
echo "   aws timestream-query query \\"
echo "     --region $REGION \\"
echo "     --query-string \"SELECT * FROM \\\"$DB_NAME\\\".\\\"$TABLE_NAME\\\" WHERE time > ago(1h)\""
echo ""
echo "2. Check S3:"
echo "   aws s3 ls s3://$BUCKET_NAME/ --recursive --human-readable"
echo ""
echo "3. Monitor IoT Rule metrics in AWS Console:"
echo "   https://console.aws.amazon.com/iot/home?region=${REGION}#/rulehub"
echo ""
echo "=================================================================="
echo "Next Steps:"
echo "  - Verify your ESP32 is publishing to 'thread/sensores'"
echo "  - Check data appears in Timestream and S3"
echo "  - Proceed to Phase 2: ML Processing"
echo "=================================================================="
